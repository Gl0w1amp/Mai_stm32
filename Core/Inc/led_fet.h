#ifndef INC_LED_FET_H_
#define INC_LED_FET_H_

#include <stdint.h>
#include "tim.h"

void FET_LED_Init();
void FET_LED_Update(uint8_t BodyLed,uint8_t ExtLed,uint8_t SideLed);

#endif /* INC_LED_FET_H_ */
