/*
 * critical_section.h
 *
 * Header-only inline PRIMASK save/restore critical-section helpers shared
 * across modules. Byte-identical to the per-module serial_lock_irq/
 * serial_unlock_irq and led_lock_irq/led_unlock_irq helpers they replace.
 */
#ifndef INC_CRITICAL_SECTION_H_
#define INC_CRITICAL_SECTION_H_

#include <stdint.h>
#include "cmsis_compiler.h"

static inline uint32_t critical_section_enter(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	return primask;
}

static inline void critical_section_exit(uint32_t primask)
{
	if (primask == 0u) {
		__enable_irq();
	}
}

#endif /* INC_CRITICAL_SECTION_H_ */
