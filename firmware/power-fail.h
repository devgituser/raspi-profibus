#ifndef POWER_FAIL_H_
#define POWER_FAIL_H_

#include "util.h"

#include <stdint.h>


/*    power fail schematic:

(Power Input)
Vin   +-------+
*---+-+ DC/DC +-o--------O------O----------------O-> Vcc = 3.3V
    | +-------+ |        |      |                |
   |-|          |       |-|    |-|               |
   |R|          |       |R|    |R|               |
   |_|     AVR  |       |_|    |_|          RPI  |
    |      +----+----+   |      |           +----+----+
    | poti |         |  ----   ----         |         |
   |-|     |         |   \/     \/          |         |
   | |<----+ PC5     |  -+-    -+-          |         |
   |_|     |         |   | gn   | red       |         |
    |      |     PD6 +---+      |           |         |
    |      |         |          |    ___    |(=pin 7) |
    |      |     PD5 +----------O---|   |-->+ GPIO4   |
   |-|     |         |    ___        ---    |(=pin 22)|
   |R|     |     PB0 +<--|   |--------------+ GPIO25  |
   |_|     |         |    ---               |         |
    |      |         |        |/----------->+/Reset   |
	|      |     PC4 +--------|             |         |
    |      +----+----+        |\            +----+----+
    |           |              |                 |
   =+=         =+=            =+=               =+=

treshold-limits:
1->0: V- = 0,3 * Vcc  --> power fail
0->1: V+ = 0,6 * Vcc  --> power good
*/

// RPI -> AVR timing
#define TWD_TGL	100	// ms toggle time
#define TWD_TOL	20	// ms tolerance time
#define TWD_CNT	10	// amount of toggles to activate the WD
#define TWD_TIMEOUT 16000	// ms = 1,6s toggle timeout
#define TWD_TSHDN 	15	// s =  5s shutdown / toggle-stop timeout
#define TWD_TPWRDN 	50000	// ms = 50s time to shut down, than reset for reboot

// io functions:
void pf_ms_tick(void);

// body functions:
void powerfail_init(void);
void powerfail_cyc(void);

#endif /* POWER_FAIL_H_ */
