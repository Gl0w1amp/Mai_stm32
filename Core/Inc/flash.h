/*
 * flash.h
 *
 *  Created on: Apr 11, 2025
 *      Author: Qinh
 */

#ifndef INC_FLASH_H_
#define INC_FLASH_H_

#include <stdint.h>

typedef union{
	uint64_t raw_flash[16];
	struct{
		union{
			uint64_t raw_block1[9];
			uint16_t touch_threshold[34];
		};
		union{
			uint64_t raw_block2[5];
			uint8_t touch_sheet[34];
		};
		union{
			uint64_t raw_block3[2];
			struct{
				uint8_t system_config;
				uint8_t delay_setting[2];
				uint8_t controller_role;
			};
		};
	};
}FlashData;

extern FlashData Flash;

uint8_t flash_write(uint64_t* data);
void flash_read(uint64_t* data);
uint8_t flash_touch_sheet_valid(const uint8_t *sheet);
uint8_t flash_config_sanitize(void);

#endif /* INC_FLASH_H_ */
