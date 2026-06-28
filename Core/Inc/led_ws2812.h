#ifndef INC_LED_WS2812_H_
#define INC_LED_WS2812_H_

#include <stdint.h>
#include "tim.h"
#include "led_types.h"

void LED_set(uint8_t led_no,uint8_t r,uint8_t g,uint8_t b);
void LED_refresh();
void LED_ServiceRefresh(void);
void led_ws2812_notify_dma_complete(void);
void led_ws2812_reset(void);

#endif /* INC_LED_WS2812_H_ */
