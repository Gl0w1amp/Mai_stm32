#ifndef _LED_H
#define _LED_H

#include "stdio.h"
#include "dma.h"
#include "tim.h"

extern uint8_t WS2812_data_raw[24];
extern uint16_t led_fade_time;
extern uint8_t led_fade_target[2];
extern uint8_t led_fade_buff[3];
extern uint8_t led_fade_flag;

void FET_LED_Init();
void FET_LED_Update(uint8_t BodyLed,uint8_t ExtLed,uint8_t SideLed);
void LED_set(uint8_t led_no,uint8_t r,uint8_t g,uint8_t b);
void LED_refresh();
void LED_UART_Init();
void LED_UART_IRQHandler();
void LED_Fade_IRQHandler();
void LED_Task_Process();
#endif
