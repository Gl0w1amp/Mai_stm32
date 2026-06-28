/*
 * benchmark.h
 *
 * Public API for the DWT-based benchmark/cycle-counter subsystem.
 */

#ifndef INC_BENCHMARK_H_
#define INC_BENCHMARK_H_

#include <stdint.h>

#define BENCHMARK_REPLY_OVERHEAD 24
#define BENCHMARK_MAX_PAYLOAD (64 - BENCHMARK_REPLY_OVERHEAD)

extern volatile uint32_t benchmark_quiet_until_ms;
extern volatile uint8_t benchmark_event_pending;
extern volatile uint32_t benchmark_event_due_ms;
extern volatile uint32_t benchmark_event_sequence;
extern volatile uint8_t benchmark_event_transport;

void benchmark_counter_init(void);
uint64_t benchmark_cycles64(void);
void benchmark_write_u32_le(uint8_t *dst, uint32_t value);
void benchmark_write_u64_le(uint8_t *dst, uint64_t value);
uint32_t benchmark_read_u32_le(const uint8_t *src);
uint8_t benchmark_quiet_active(void);
void benchmark_emit_pending_event(void);

#endif /* INC_BENCHMARK_H_ */
