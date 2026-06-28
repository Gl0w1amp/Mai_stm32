/*
 * app_state.h
 *
 * Owner header for the cross-module application-state globals.
 *
 * Concurrency ownership (see the shared-state audit):
 *  - player: controller role (1P/2P). Written at startup by Touch_Task (from
 *    Flash.controller_role) and by Command_Task on a role-change command. It is
 *    effectively write-only - the live USB role is driven by
 *    Flash.controller_role / USBD_SetControllerRole, not by reading this. Single
 *    aligned byte, so the unsynchronized writes are atomic on Cortex-M4.
 *  - current_touch_status: Touch_Task-private scratch (written and read only
 *    within one Touch_Task loop iteration). Cross-task touch delivery goes through
 *    the critical-section-guarded input_snapshot, not this array.
 */

#ifndef INC_APP_STATE_H_
#define INC_APP_STATE_H_

#include <stdint.h>

extern uint8_t player;
extern uint8_t current_touch_status[34];

#endif /* INC_APP_STATE_H_ */
