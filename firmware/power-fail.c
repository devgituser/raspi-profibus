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

#define _INLINE_FUNC_	__attribute__ ((always_inline))

// -------------- type defs --------------
typedef enum
{
	PWS_INIT = 0,
	PWS_PWRUP,PWS_CHECK,PWS_ALERT,
	PWS_AMOUNT
}tde_pwf_states;

typedef struct
{
	int16_t timer_ms;		// counts milli seconds
	int8_t  timer_s;		// counts seconds
	tde_pwf_states state;	// state of power fail - FSM
}tds_main;
tds_main gs_main;

// -------------- prototypes --------------
inline void pf_state(tde_pwf_states);

// -------------- io functions ------------

inline void pf_ms_tick(void)
{
	gs_main.timer_ms++;
}

// -------------- body functions ----------

inline void powerfail_init(void)
{	
	memset(&gs_main,0x00,sizeof(gs_main));
	// input
	PWF_DIR &= PWF_BIT;	// set gpio to input
	PWF_PORT&= PWF_BIT;	// turn off pullup
	// altert
	RPA_LED_RED_OFF();
	RPA_LED_GRN_OFF();
	RPA_DIR |= (RPA_BIT|RPA_LED);	// set output
}

void pf_state(tde_pwf_states state)
{
	if(state < PWS_AMOUNT){
		gs_main.state = state;
	}
}

inline void powerfail_cyc(void)
{
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
