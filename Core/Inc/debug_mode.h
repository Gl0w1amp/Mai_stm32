/*
 * debug_mode.h
 *
 * Owner header for the debug / serial-mode globals shared across the
 * capsense, serial-command, USB-CDC, and FreeRTOS application layers.
 */

#ifndef INC_DEBUG_MODE_H_
#define INC_DEBUG_MODE_H_

#include <stdint.h>

extern volatile uint8_t debug_flag;
extern volatile uint8_t debug_stream_mode;
extern uint8_t debug_channel;

#endif /* INC_DEBUG_MODE_H_ */
