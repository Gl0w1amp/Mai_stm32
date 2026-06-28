#ifndef _LED_H
#define _LED_H

#include "stdio.h"
#include <stdint.h>
#include "dma.h"
#include "tim.h"

typedef enum {
  LED_MODE_AUTO = 0,
  LED_MODE_HOST_CONTROLLED = 1,
  LED_MODE_OFF = 2,
  LED_MODE_BOOT = 3,
  LED_MODE_IDLE = 4,
  LED_MODE_INPUT_REACTIVE = 5,
  LED_MODE_DIAGNOSTIC = 6,
  LED_MODE_ERROR = 7
} LED_Mode;

typedef struct {
  uint8_t mode;
  uint8_t host_active;
  uint8_t idle_effect;
  uint8_t idle_brightness;
  uint16_t host_timeout_ms;
  uint16_t host_remaining_ms;
} LED_Status;

extern uint8_t WS2812_data_raw[24];
extern uint16_t led_fade_time;
extern uint8_t led_fade_target[2];
extern uint8_t led_fade_flag;
extern uint8_t led_fade_color[2][3];
extern uint16_t led_fade_clock;

void LED_UART_IRQHandler();

#include "mai2led.h"
#include "led_ws2812.h"
#include "led_state.h"
#include "led_fet.h"

#endif
