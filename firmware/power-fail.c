/*
 * PROFIBUS DP - power fail monitor
 *
 * Copyright (c) 2015 K.H.L. <git.user@web.de>
 *
 * Licensed under the terms of the GNU General Public License version 2,
 * or (at your option) any later version.
 */

#include "power-fail.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>			// for memset()

	
// power fail pin monitors input voltage
#define PWF_DIR		DDRC
#define PWF_IN		PINC
#define PWF_PORT	PORTC
#define PWF_BIT		(1<<5)
#define PWF_IS_ON()	(PWF_IN&PWF_BIT)// true @ power good

// RPI alarm pin alterts the rpi
#define RPA_DIR		DDRD
#define RPA_IN		PIND
#define RPA_PORT	PORTD
#define RPA_BIT		(1<<5)	// red alert pin to rpi
#define RPA_LED_RED_ON() 	RPA_PORT&=~RPA_BIT
#define RPA_LED_RED_OFF() 	RPA_PORT|=RPA_BIT
#define RPA_LED_RED_TGL() 	RPA_PORT^=RPA_BIT
#define RPA_LED		(1<<6)	// just an info led
#define RPA_LED_GRN_ON() 	RPA_PORT&=~RPA_LED
#define RPA_LED_GRN_OFF() 	RPA_PORT|=RPA_LED
#define RPA_LED_GRN_TGL() 	RPA_PORT^=RPA_LED

// RPI reset pin
#define RPR_DIR		DDRC
#define RPR_PORT	PORTC
#define RPR_BIT		(1<<4)	// red alert pin to rpi
#define RPR_RESET_OFF()	RPR_PORT&=~RPR_BIT
#define RPR_RESET_ON() 	RPR_PORT|=RPR_BIT

// RPI WD pin PC4
#define RPT_DIR		DDRB
#define RPT_PORT	PORTB
#define RPT_IN		PINB
#define RPT_BIT		(1<<0)	// red alert pin to rpi
#define RPT_IS_ON()	(RPT_IN&RPT_BIT)// driven by RPI

// -------------- type defs --------------
typedef enum
{
	PWS_INIT = 0,
	PWS_PWRUP,PWS_CHECK,PWS_ALERT,
	PWS_AMOUNT
}tde_pwf_states;

typedef enum
{
	WDS_INIT = 0,
	WDS_WTGL0,	// wait for toggle
	WDS_WTGL1,	// wait for toggle
	WDS_WTO_0,	// wait for timeout (prepare)
	WDS_WTO_1,	// wait for timeout (prepare)
	WDS_WTA_0,	// wait for timeout (active)
	WDS_WTA_1,	// wait for timeout (active)
	WDS_SHDN0,	// soft shutdown!
	WDS_SHDN1,	// soft shutdown!
	WDS_WSHDN,	// rpi is shutting down, wait some time
	WDS_RST,	// reset!
	WDS_AMOUNT
}tde_wd_states;

typedef struct
{
	// power fail
	tde_pwf_states state;	// state of power fail - FSM
	uint8_t  timer_s;		// counts seconds
	uint16_t timer_ms;		// counts milli seconds
	// watchdog
	uint16_t t_wd_ms;		// counts milli seconds
	uint8_t  t_wd_s;			// counts seconds to toggle
	uint8_t  t_rst_s;		// counts seconds to hard reset
	tde_wd_states wds;		// state of watchdog - FSM
	uint8_t  tgl_cnt;		// counts toggles
}tds_main;
tds_main gs_main;

// -------------- prototypes --------------
static inline void wd_reset_all_timers(void);
static inline void wd_state(tde_wd_states );
static inline void pf_state(tde_pwf_states);
static inline uint8_t pf_is_alert(void);
static inline void pf_set_alert(void);
static inline void wd_cyc(void);

// -------------- io functions ------------

inline void pf_ms_tick(void)
{
	gs_main.timer_ms++;
	gs_main.t_wd_ms++;
}

// -------------- body functions ----------

static inline void wd_reset_all_timers(void)
{
		gs_main.tgl_cnt=0;
		gs_main.t_wd_ms=0;
		gs_main.t_wd_s=0;
		gs_main.t_rst_s=0;
		return;
}

static inline void wd_state(tde_wd_states state)
{
	if(state < WDS_AMOUNT){
		gs_main.wds = state;
	}
}

static inline void wd_cyc(void)
{
	if( pf_is_alert() )
	{
		// never perform a reset @ power fail
		wd_reset_all_timers();
		wd_state(WDS_WSHDN);
		return;
	}
	switch(gs_main.wds)
	{
	case WDS_INIT:
	{
		wd_reset_all_timers();
		wd_state(WDS_WTGL0);
	}
	case WDS_WTGL0:	// wait for sequence (10x 5Hz toggle)
	{	
		if( pf_is_alert() )
		{
			wd_reset_all_timers();
			wd_state(WDS_WSHDN);
			break;
		}
		if( RPT_IS_ON() == 0 )
		{
			wd_state(WDS_WTGL1);
			if(( gs_main.t_wd_ms <= (TWD_TGL+TWD_TOL)) && \
				 gs_main.t_wd_ms >= (TWD_TGL-TWD_TOL) )
			{
				gs_main.tgl_cnt++;
				if(gs_main.tgl_cnt >= (TWD_CNT) )
				{
					gs_main.t_wd_ms = 0;
					wd_state(WDS_WTO_1); // activate WD!
				}
				else
				{
					gs_main.t_wd_ms = 0;
				}
			}
			else
			{
				gs_main.t_wd_ms = 0;
				gs_main.tgl_cnt=0;
			}
		}
		else
		{
			if(gs_main.t_wd_ms > (TWD_TGL+TWD_TOL))
			{
				gs_main.t_wd_ms = 0;
				gs_main.tgl_cnt=0;
			}
		}
	}break;
	case WDS_WTGL1:	// wait for sequence (10x 5Hz toggle)
	{
		if( pf_is_alert() )
		{
			wd_reset_all_timers();
			wd_state(WDS_WSHDN);
			break;
		}
		if( RPT_IS_ON() )
		{
			wd_state(WDS_WTGL0);
			if(( gs_main.t_wd_ms <= (TWD_TGL+TWD_TOL)) && \
				 gs_main.t_wd_ms >= (TWD_TGL-TWD_TOL) )
			{
				gs_main.tgl_cnt++;
				if(gs_main.tgl_cnt >= (TWD_CNT) )
				{
					gs_main.t_wd_ms = 0;
					wd_state(WDS_WTO_0); // activate WD!
				}
				else
				{
					gs_main.t_wd_ms = 0;
				}
			}
			else
			{
				gs_main.t_wd_ms = 0;
				gs_main.tgl_cnt=0;
			}
		}
		else
		{
			if(gs_main.t_wd_ms > (TWD_TGL+TWD_TOL))
			{
				gs_main.t_wd_ms = 0;
				gs_main.tgl_cnt=0;
			}
		}
	}break;
	case WDS_WTO_0:	// prepare watchdog
	{	
		wd_reset_all_timers();
		wd_state(WDS_WTA_0);
	}break;
	case WDS_WTO_1:	// prepare watchdog
	{		
		wd_reset_all_timers();
		wd_state(WDS_WTA_1);
	}break;
	case WDS_WTA_0:	// watch for neg toggle timeout
	{	
		if( pf_is_alert() )	// Power fail -> already alerting RPI
		{
			wd_reset_all_timers();
			wd_state(WDS_WSHDN);
			break;
		}
		if(gs_main.t_wd_ms > (TWD_TIMEOUT))	
		{	// no toggle in last 16s --> try soft shutdown
			gs_main.t_wd_ms = 0;
			gs_main.t_wd_s = 0;
			gs_main.t_rst_s = 0;
			pf_set_alert();	// set power fail stm into alert mode
			wd_state(WDS_SHDN1);
			break;
		}
		if( RPT_IS_ON() == 0 )
		{
			gs_main.t_wd_ms = 0;	// retrigger WD
			gs_main.t_wd_s = 0;
			gs_main.t_rst_s = 0;
			wd_state(WDS_WTA_1);
		}	
	}break;
	case WDS_WTA_1:	// watch for pos toggle timeout
	{	
		if( pf_is_alert() )	// Power fail -> already alerting RPI
		{
			wd_reset_all_timers();
			wd_state(WDS_WSHDN);
			break;
		}
		if(gs_main.t_wd_ms > (TWD_TIMEOUT))
		{	// no toggle in last 16s --> try soft shutdown
			wd_reset_all_timers();
			pf_set_alert();	// set power fail stm into alert mode
			wd_state(WDS_SHDN0);
			break;
		}
		if( RPT_IS_ON() )
		{
			wd_reset_all_timers();	// retrigger WD
			wd_state(WDS_WTA_0);
		}	
	}break;
	case WDS_SHDN0:	// soft shutdown, wait for toggle stop
	{	
		// wait for toggle stop, otherwise perform reset
		if(gs_main.t_wd_ms >= 1000 )
		{
			gs_main.t_wd_ms = 0;
			gs_main.t_wd_s++;
			gs_main.t_rst_s++;
			if( gs_main.t_rst_s >= TWD_TSHDN) // don't wait for toggle stop
			{
				wd_reset_all_timers();
				wd_state(WDS_RST);		// perform hard reset!
				break;
			}
		}
		if( RPT_IS_ON() == 0 )
		{
			wd_state(WDS_SHDN1);
			gs_main.t_wd_s = 0;	// toggle continues
		}
		else
		{
			if(gs_main.t_wd_s > (TWD_TIMEOUT/1000) )	// toggle stopped!
			{
				wd_reset_all_timers();
				wd_state(WDS_WSHDN);
				break;
			}
		}
	}break;
	case WDS_SHDN1:	// soft shutdown, wait for toggle stop
	{	
		// wait for toggle stop, otherwise perform reset
		if(gs_main.t_wd_ms >= 1000 )
		{
			gs_main.t_wd_ms = 0;
			gs_main.t_wd_s++;
			gs_main.t_rst_s++;
			if( gs_main.t_rst_s >= TWD_TSHDN) // give up waiting for toggle to stop
			{
				wd_reset_all_timers();
				wd_state(WDS_RST);		// perform hard reset!
				break;
			}
		}
		if( RPT_IS_ON() )
		{
			wd_state(WDS_SHDN0);
			gs_main.t_wd_s = 0;	// toggle continues
		}
		else
		{
			if(gs_main.t_wd_s > (TWD_TIMEOUT/1000) )	// toggle stopped!
			{
				wd_reset_all_timers();
				wd_state(WDS_WSHDN);
				break;
			}
		}
	}break;
	case WDS_WSHDN:	// wait for rpi to shutdown
	{
		if(pf_is_alert() == 0)			// wait for end of power fail
		{
			if( (gs_main.t_wd_ms >= TWD_TPWRDN )  )
			{
				wd_reset_all_timers();
				wd_state(WDS_RST);
			}
		}
		else
		{
			gs_main.t_wd_ms = 0;
		}
	}break;
	case WDS_RST:	// perform hard reset
	{
		if( gs_main.t_wd_ms <= 500 ) // 500 ms pulse
		{
			RPR_RESET_ON();
		}
		else
		{
			wd_reset_all_timers();
			RPR_RESET_OFF();
			wd_state(WDS_INIT);
			powerfail_init();
		}
	}break;
	default:
	{
	}}
}

inline void powerfail_init(void)
{	
	memset(&gs_main,0x00,sizeof(gs_main));
	// reset - pin
	RPR_RESET_OFF();
	RPR_DIR |= RPR_BIT;
	// watchdog toggle pin
	RPT_DIR &=~RPT_BIT;
	RPT_PORT|= RPT_BIT;	// pullup active
	// input
	PWF_DIR &=~PWF_BIT;	// set gpio to input
	PWF_PORT&=~PWF_BIT;	// turn off pullup
	// altert
	RPA_LED_RED_OFF();
	RPA_LED_GRN_OFF();
	RPA_DIR |= (RPA_BIT|RPA_LED);	// set output
}

static inline void pf_state(tde_pwf_states state)
{
	if(state < PWS_AMOUNT){
		gs_main.state = state;
	}
}

static inline uint8_t pf_is_alert(void)
{
	return (gs_main.state == PWS_ALERT);
}

static inline void pf_set_alert(void)
{
	gs_main.timer_ms = 0;
	gs_main.timer_s = 0;
	pf_state(PWS_ALERT);
}

inline void powerfail_cyc(void)
{
	wd_cyc();
	switch(gs_main.state){
	case PWS_INIT:
	{
		powerfail_init();
		pf_state(PWS_PWRUP);
		gs_main.timer_ms =0;
		gs_main.timer_s =0;
	}
	break;
	case PWS_PWRUP:
	{
		if(gs_main.timer_ms >= 250)
		{
			gs_main.timer_ms = 0;
			gs_main.timer_s++;
			if( gs_main.timer_s >= 4*4)	// 4s to power up
			{
				pf_state(PWS_CHECK);
			}
			
			if( PWF_IS_ON() )
			{
				RPA_LED_GRN_TGL();	// LED toggle while boot
			}
			else
			{
				RPA_LED_GRN_OFF();	// LED off	
			}
		}
	}
	break;
	// 10s after power up: start power monitoring
	case PWS_CHECK:
	{
		RPA_LED_RED_OFF();	// const. H-Level = no alert		
		if( PWF_IS_ON() == 0)
		{
			// power fail detected
			pf_state(PWS_ALERT);
			gs_main.timer_ms = 0;
			gs_main.timer_s = 0;
			RPA_LED_GRN_OFF();	// green LED off
		}
		else
		{
			RPA_LED_GRN_ON();
		}			
	}
	break;
	// power fail detected --> toggle
	case PWS_ALERT:
	{	
		if( PWF_IS_ON() )
		{
			RPA_LED_GRN_ON();	// LED on			
		}
		else
		{
			RPA_LED_GRN_OFF();	// LED off
			gs_main.timer_s = 0;	
		}
		if(gs_main.timer_ms >= 100) // f = 5Hz
		{
			gs_main.timer_ms = 0;
			RPA_LED_RED_TGL();	// toggel alert pin
						
			// power good again?
			if( PWF_IS_ON() )
			{
				gs_main.timer_s++;
				if(	gs_main.timer_s >= (10*4) )
				{
					// power stable for over 10s
					pf_state(PWS_CHECK);
					gs_main.timer_ms = 0;
					gs_main.timer_s = 0;
				}		
			}
		}
	}
	break;
	default:
	{
		pf_state(PWS_INIT);
	}}
}
