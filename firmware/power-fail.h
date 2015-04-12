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
    |      |         |          |    ___    |         |
   |-|     |     PD5 +----------O---|   |-->+ GPIO4   |
   |R|     |         |               ---    |(=pin 7) |
   |_|     +----+----+                      +----+----+
    |           |                                |
   =+=         =+=                              =+=

treshold-limits:
1->0: V- = 0,3 * Vcc  --> power fail
0->1: V+ = 0,6 * Vcc  --> power good
*/

// io functions:
void pf_ms_tick(void);

// body functions:
void powerfail_init(void);
void powerfail_cyc(void);

#endif /* POWER_FAIL_H_ */
