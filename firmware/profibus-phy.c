/*
 * PROFIBUS DP - PHY
 *
 * Copyright (c) 2013 Michael Buesch <m@bues.ch>
 *
 * Licensed under the terms of the GNU General Public License version 2,
 * or (at your option) any later version.
 */

#include "profibus-phy.h"
#include "util.h"

#include <stdlib.h>
#include <stdint.h>

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>


#define PB_MAX_TELEGRAM_SIZE	255


/* RTS signal */
#define RTS_DDR		DDRD
#define RTS_PORT	PORTD
#define RTS_BIT		2

/* Activity LED */
#define LED_ACT_DDR	DDRD
#define LED_ACT_PORT	PORTD
#define LED_ACT_BIT	7


struct ubrr_value {
	uint16_t ubrr;
	uint8_t u2x;
};

#define DROUND(val)	(			\
		((val) >= (double)0.0) ? (	\
			(long)((val) + 0.5)	\
		) : (				\
			(long)((val) - 0.5)	\
		)				\
	)

#define BAUD_TO_UBRR(baud, u2x)	\
	_max(DROUND((double)F_CPU / (((u2x) ? 8 : 16) * (double)(baud))) - 1, 0)

#define UBRR_TO_BAUD(ubrr, u2x)	\
	DROUND((double)F_CPU / (((u2x) ? 8 : 16) * ((double)(ubrr) + 1)))

#define UBRR_ERR(baud, u2x)	\
	(long)(baud) - UBRR_TO_BAUD(BAUD_TO_UBRR(baud, u2x), u2x)

#define USE_2X(baud)		(_generic_abs(UBRR_ERR(baud, 1)) < _generic_abs(UBRR_ERR(baud, 0)))

#define UBRR_ENTRY(baud)	{					\
		.ubrr	= BAUD_TO_UBRR(baud, USE_2X(baud)),		\
		.u2x	= USE_2X(baud),					\
	}

static const struct ubrr_value PROGMEM baud_to_ubrr[] = {
	[ PB_PHY_BAUD_9600 ]		= UBRR_ENTRY(9600),
	[ PB_PHY_BAUD_19200 ]		= UBRR_ENTRY(19200),
	[ PB_PHY_BAUD_45450 ]		= UBRR_ENTRY(45450),
	[ PB_PHY_BAUD_93750 ]		= UBRR_ENTRY(93750),
	[ PB_PHY_BAUD_187500 ]		= UBRR_ENTRY(187500),
	[ PB_PHY_BAUD_500000 ]		= UBRR_ENTRY(500000),
	[ PB_PHY_BAUD_1500000 ]		= UBRR_ENTRY(1500000),
	[ PB_PHY_BAUD_3000000 ]		= UBRR_ENTRY(3000000),
	[ PB_PHY_BAUD_6000000 ]		= UBRR_ENTRY(6000000),
	[ PB_PHY_BAUD_12000000 ]	= UBRR_ENTRY(12000000),
};

enum pb_state {
	PB_IDLE,
	PB_SENDING_SRD,
	PB_SENDING_SDN,
	PB_RECEIVING_SRD,
};

struct pb_context {
	enum pb_state state;
	const struct pb_telegram *request;
	struct pb_telegram *reply;
	uint8_t size;
	uint8_t byte_ptr;
	uint8_t reply_timeout;
	uint8_t reply_timeout_count;
	bool check_bit_errors;
	bool tail_wait;
	pb_notifier_t notifier;
	enum pb_phy_baud baudrate;
	#if PB_PHY_RTS_ALWAYS == 1
		// here we don't need the flag
	#else
		enum pb_phy_rtsmode rtsmode;
	#endif
};

static struct pb_context profibus;


static void set_rts(bool on)
{
#if PB_PHY_RTS_ALWAYS == 1
	if (on)
		RTS_PORT |= (1 << RTS_BIT);
	else
		RTS_PORT &= ~(1 << RTS_BIT);
	// enables the compiler to speed up
#else
	switch (profibus.rtsmode) {
	default:
	case PB_PHY_RTS_ALWAYS_LO:
		RTS_PORT &= ~(1 << RTS_BIT);
		break;
	case PB_PHY_RTS_ALWAYS_HI:
		RTS_PORT |= (1 << RTS_BIT);
		break;
	case PB_PHY_RTS_SENDING_HI:
		if (on)
			RTS_PORT |= (1 << RTS_BIT);
		else
			RTS_PORT &= ~(1 << RTS_BIT);
		break;
	case PB_PHY_RTS_SENDING_LO:
		if (on)
			RTS_PORT &= ~(1 << RTS_BIT);
		else
			RTS_PORT |= (1 << RTS_BIT);
		break;
	}
#endif
}

static void set_activity_led(bool on)
{
	if (on)
		LED_ACT_PORT |= (1 << LED_ACT_BIT);
	else
		LED_ACT_PORT &= ~(1 << LED_ACT_BIT);
}

static void pb_notify(enum pb_event event, uint8_t value)
{
	if (profibus.notifier)
		profibus.notifier(event, value);
}

static uint8_t pb_telegram_size(const struct pb_telegram *t)
{
	switch (t->sd) {
	case PB_SD1:
		return 6;
	case PB_SD2:
		if (t->var.le != t->var.ler)
			break;
		if (t->var.le < 4 || t->var.le > 249)
			break;
		return t->var.le + 6;
	case PB_SD3:
		return 14;
	case PB_SD4:
		return 3;
	case PB_SC:
		return 1;
	}

	return 0; /* error */
}

static void pb_tx_next(void)
{
	const uint8_t *t;
	uint8_t byte;

	if (!(UCSR0A & (1 << UDRE0)))
		return;

	t = (const uint8_t *)profibus.request;
	byte = t[profibus.byte_ptr];
	profibus.byte_ptr++;

	UDR0 = byte;
}

static void receiver_enable(void)
{
	(void)UDR0;
	UCSR0B |= (1 << RXCIE0);
	UCSR0B |= (1 << RXEN0);
}

static void receiver_disable(void)
{
	UCSR0B &= ~(1 << RXCIE0);
	UCSR0B &= ~(1 << RXEN0);
	PORTD &= ~(1 << PD0);
	DDRD &= ~(1 << DDD0);
}

/* TX-complete interrupt */
ISR(USART_TX_vect)
{
	mb();

	if (!profibus.tail_wait)
		goto out;
	profibus.tail_wait = 0;

	if (profibus.state == PB_SENDING_SRD) {
		/* Transmission complete. Prepare to receive reply. */
		set_rts(0);
		receiver_enable();
		profibus.state = PB_RECEIVING_SRD;
		profibus.size = 0;
		profibus.byte_ptr = 0;
		profibus.reply_timeout_count = profibus.reply_timeout;
		pb_notify(PB_EV_SRD_SENT, 0);
	} else if (profibus.state == PB_SENDING_SDN) {
		/* Transmission complete. Call notifier. */
		profibus.state = PB_IDLE;
		set_activity_led(0);
		pb_notify(PB_EV_SDN_COMPLETE, 0);
	}

out:
	mb();
}

/* UDR-empty interrupt */
ISR(USART_UDRE_vect)
{
	mb();

	if (profibus.tail_wait)
		goto out;
	if (profibus.byte_ptr >= profibus.size) {
		/* All bytes are queued for transmission. */
		UCSR0B &= ~(1 << UDRIE0);
		profibus.tail_wait = 1;
		goto out;
	}
	/* Queue the next byte. */
	pb_tx_next();

out:
	mb();
}

static void receive_finish(bool error)
{
	receiver_disable();
	profibus.state = PB_IDLE;
	set_activity_led(0);
	pb_notify(error ? PB_EV_SRD_ERROR : PB_EV_SRD_COMPLETE,
		  profibus.size);
}

static void receive_complete(void)
{
	receive_finish(0);
}

static void receive_error(void)
{
	receive_finish(1);
}

/* RX-complete interrupt */
ISR(USART_RX_vect)
{
	uint8_t *t;
	uint8_t data, status, error;

	mb();

	status = UCSR0A;
	data = UDR0;

	error = status & ((1 << FE0) | (1 << UPE0) | (1 << DOR0));
	if (error && profibus.check_bit_errors) {
		receive_error();
		goto out;
	}

	t = (uint8_t *)profibus.reply;
	t[profibus.byte_ptr] = data;
	profibus.byte_ptr++;

	if (profibus.byte_ptr == 1) {
		if (profibus.reply->sd != PB_SD2) {
			profibus.size = pb_telegram_size(profibus.reply);
			if (!profibus.size) {
				receive_error();
				goto out;
			}
		}
	}
	if (profibus.size) {
		if (profibus.byte_ptr >= profibus.size) {
			receive_complete();
			goto out;
		}
	}
	if (!profibus.size && profibus.byte_ptr == 3 &&
	    profibus.reply->sd == PB_SD2) {
		profibus.size = pb_telegram_size(profibus.reply);
		if (!profibus.size) {
			receive_error();
			goto out;
		}
	}

out:
	mb();
}

static int8_t pb_transfer(const struct pb_telegram *request,
			  struct pb_telegram *reply,
			  enum pb_state state)
{
	uint8_t sreg, request_size;
	int8_t err = 0;

	request_size = pb_telegram_size(request);
	if (!request_size) {
		err = -1;
		goto out;
	}

	sreg = irq_disable_save();

	if (profibus.state != PB_IDLE) {
		err = -1;
		goto out_restore;
	}

	profibus.state = state;
	profibus.request = request;
	profibus.reply = reply;
	profibus.size = request_size;
	profibus.byte_ptr = 0;
	profibus.tail_wait = 0;

	set_activity_led(1);

	UCSR0B |= (1 << UDRIE0);
	set_rts(1);
	pb_tx_next();

out_restore:
	irq_restore(sreg);
out:

	return err;
}

void pb_reset(void)
{
	uint8_t sreg;

	sreg = irq_disable_save();

	profibus.state = PB_IDLE;
	profibus.request = NULL;
	profibus.reply = NULL;
	profibus.size = 0;
	profibus.byte_ptr = 0;
	profibus.tail_wait = 0;

	set_rts(0);
	UCSR0B &= ~(1 << UDRIE0);
	receiver_disable();

	irq_restore(sreg);
}

int8_t pb_srd(const struct pb_telegram *request,
	      struct pb_telegram *reply)
{
	return pb_transfer(request, reply, PB_SENDING_SRD);
}

int8_t pb_sdn(const struct pb_telegram *request)
{
	return pb_transfer(request, NULL, PB_SENDING_SDN);
}

void pb_enable_biterror_checks(bool enable)
{
	uint8_t sreg;

	sreg = irq_disable_save();
	profibus.check_bit_errors = enable;
	irq_restore(sreg);
}

void pb_set_notifier(pb_notifier_t notifier)
{
	uint8_t sreg;

	sreg = irq_disable_save();
	profibus.notifier = notifier;
	irq_restore(sreg);
}

void pb_set_rx_timeout(uint8_t ms)
{
	uint8_t sreg;

	sreg = irq_disable_save();
	profibus.reply_timeout = max(ms, 1);
	irq_restore(sreg);
}

inline void pb_ms_tick(void)
{
	uint8_t sreg;

	sreg = irq_disable_save();

	if (profibus.state == PB_RECEIVING_SRD) {
		profibus.reply_timeout_count--;
		if (profibus.reply_timeout_count == 0)
			receive_error();
	}

	irq_restore(sreg);
}

int8_t pb_set_baudrate(enum pb_phy_baud baudrate)
{
	struct ubrr_value ubrr;
	int8_t err = 0;
	uint8_t sreg;

	sreg = irq_disable_save();

	if (baudrate >= ARRAY_SIZE(baud_to_ubrr)) {
		err = -1;
		goto out;
	}
	memcpy_P(&ubrr, &baud_to_ubrr[baudrate], sizeof(ubrr));

	if (!ubrr.ubrr) {
		err = -2;
		goto out;
	}

	UBRR0 = ubrr.ubrr;
	if (ubrr.u2x)
		UCSR0A |= (1 << U2X0);
	else
		UCSR0A &= ~(1 << U2X0);
	profibus.baudrate = baudrate;

out:
	irq_restore(sreg);

	return err;
}

enum pb_phy_baud pb_get_baudrate(void)
{
	enum pb_phy_baud baudrate;
	uint8_t sreg;

	sreg = irq_disable_save();
	baudrate = profibus.baudrate;
	irq_restore(sreg);

	return baudrate;
}

int8_t pb_set_rtsmode(enum pb_phy_rtsmode mode)
{
#if PB_PHY_RTS_ALWAYS == 1
	return 0;
#else
	uint8_t sreg;

	if (mode >= PB_PHY_RTS_NR_MODES)
		return -1;

	sreg = irq_disable_save();
	profibus.rtsmode = mode;
	set_rts(0);
	irq_restore(sreg);

	return 0;
#endif
}

void pb_phy_init(void)
{
	uint8_t sreg;

	sreg = irq_disable_save();

	/* Initialize RTS signal */
	RTS_DDR |= (1 << RTS_BIT);
	pb_set_rtsmode(PB_PHY_RTS_ALWAYS_LO);

	/* Initialize activity LED */
	LED_ACT_DDR |= (1 << LED_ACT_BIT);
	set_activity_led(0);

	/* Initialize UART */
	UCSR0A = 0;
	/* 8 data bits, 1 stop bit, even parity */
	UCSR0C = (1 << UCSZ00) | (1 << UCSZ01) | (1 << UPM01);
	/* Enable transmitter. */
	UCSR0B = (1 << TXEN0) | (1 << TXCIE0);
	pb_set_baudrate(PB_PHY_BAUD_19200);
	receiver_disable();

	/* Reset state */
	pb_reset();
	pb_set_rx_timeout(100);
	pb_enable_biterror_checks(1);
	profibus.notifier = NULL;

	irq_restore(sreg);
}

void pb_phy_exit(void)
{
	uint8_t sreg;

	sreg = irq_disable_save();

	pb_reset();
	set_rts(0);
	UCSR0A = 0;
	UCSR0B = 0;
	UCSR0C = 0;

	irq_restore(sreg);
}
