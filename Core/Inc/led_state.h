#ifndef INC_LED_STATE_H_
#define INC_LED_STATE_H_

#include <stdint.h>
#include "led_types.h"

/* State-machine / fade-engine public API */
uint8_t LED_StateLockInit(void);
uint8_t LED_StateLock(void);
void LED_StateUnlock(void);
void LED_update_button(uint8_t speed);
void LED_update_button_rgb_speed(const uint8_t *rgb_speed, uint8_t count);
void LED_update_button_rgb_fade(const uint8_t *rgb_fade, uint8_t count);
void LED_SetButtonFrame(const uint8_t rgb[24]);
void LED_StateMachineInit(uint32_t now);
void LED_ServiceStateMachine(uint32_t now);
void LED_NotifyHostControl(uint32_t now);
uint8_t LED_SetMode(uint8_t mode, uint32_t now);
uint8_t LED_ConfigSet(uint8_t idle_effect, uint8_t idle_brightness,
    uint16_t host_timeout_ms);
void LED_StatusSnapshot(LED_Status *status, uint32_t now);
void LED_ServiceFade(void);

/* Cross-TU seam consumed by mai2led.c */
void set_led_immediate(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void set_led_fade(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t speed);
void schedule_led_fade(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t speed);
void start_pending_fades(void);
uint8_t resolve_multi_len(uint8_t start, uint8_t end_field);

#endif /* INC_LED_STATE_H_ */
