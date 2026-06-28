#ifndef INC_LED_TYPES_H_
#define INC_LED_TYPES_H_

#include <stdint.h>

/* Shared LED geometry. The physical strip has NUM_LED WS2812 dies wired
 * PRE_BUTTON_LED per logical button, giving BUTTON_LED_COUNT buttons. */
#define NUM_LED 16
#define PRE_BUTTON_LED 2
#define BUTTON_LED_COUNT (NUM_LED / PRE_BUTTON_LED)

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

#endif /* INC_LED_TYPES_H_ */
