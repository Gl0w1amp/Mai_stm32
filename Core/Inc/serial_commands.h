/*
 * serial_commands.h
 *
 * Command dispatch for parsed CDC serial frames.
 */

#ifndef INC_SERIAL_COMMANDS_H_
#define INC_SERIAL_COMMANDS_H_

#include "serial_protocol.h"
#include <stdint.h>

void serial_commands_process_frame(const serial_frame_t *frame,
		uint64_t dispatch_cycles);
void serial_commands_process_legacy_ascii(const uint8_t *rx_buffer,
		uint8_t rx_len);

void heart_beat_refresh(void);
uint8_t heart_beat_active(void);
uint8_t controller_role_normalize(uint8_t role);
void debug_exit_reset_now(void);
void command_notify_ready_from_isr(void);

#endif /* INC_SERIAL_COMMANDS_H_ */
