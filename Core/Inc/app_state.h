/*
 * app_state.h
 *
 * Owner header for the cross-module application-state globals
 * `player` and `current_touch_status`.
 */

#ifndef INC_APP_STATE_H_
#define INC_APP_STATE_H_

#include <stdint.h>

extern uint8_t player;
extern uint8_t current_touch_status[34];

#endif /* INC_APP_STATE_H_ */
