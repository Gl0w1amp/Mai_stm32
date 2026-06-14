/*
 * flash.c
 *
 *  Created on: Apr 11, 2025
 *      Author: Qinh
 */
#include <stdint.h>
#include <string.h>
#include "stm32g4xx_hal.h"
#include "stm32g4xx_hal_flash.h"
#include "stm32g4xx_hal_flash_ex.h"
#include "flash.h"

#define G431_FLASH_BASE             0x08000000UL
#define G431_FLASH_SIZE             0x20000UL
#define G431_APP_SETTINGS_BASE      0x0801D000UL
#define G431_TARGET_DOUBLEWORD      ((uint32_t)(sizeof(FlashData) / sizeof(uint64_t)))

FlashData Flash;

static uint8_t g431_flash_uses_dual_bank(void)
{
#if defined(FLASH_OPTR_DBANK)
	return (READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) != 0U) ? 1u : 0u;
#else
	return 0u;
#endif
}

static uint32_t g431_flash_page_size(void)
{
#if defined(FLASH_OPTR_DBANK) && defined(FLASH_PAGE_SIZE_128_BITS)
	if (g431_flash_uses_dual_bank() == 0u) {
		return FLASH_PAGE_SIZE_128_BITS;
	}
#endif

	return FLASH_PAGE_SIZE;
}

static uint32_t g431_flash_bank_size(void)
{
	return (g431_flash_uses_dual_bank() != 0u) ? (G431_FLASH_SIZE / 2u) : G431_FLASH_SIZE;
}

static uint32_t g431_flash_bank_for_address(uint32_t address)
{
#if defined(FLASH_BANK_2)
	uint32_t offset = address - G431_FLASH_BASE;
	return ((g431_flash_uses_dual_bank() != 0u) && (offset >= g431_flash_bank_size())) ?
			FLASH_BANK_2 : FLASH_BANK_1;
#else
	(void)address;
	return FLASH_BANK_1;
#endif
}

static uint32_t g431_flash_page_for_address(uint32_t address)
{
	uint32_t offset = address - G431_FLASH_BASE;
	uint32_t bank_size = g431_flash_bank_size();

	if ((g431_flash_uses_dual_bank() != 0u) && (offset >= bank_size)) {
		offset -= bank_size;
	}

	return offset / g431_flash_page_size();
}

uint8_t flash_write(uint64_t* data){
	if (data == NULL) {
		return 0u;
	}

	if (HAL_FLASH_Unlock() != HAL_OK) {
		return 0u;
	}

	FLASH_EraseInitTypeDef Erase = {0};
	Erase.TypeErase = FLASH_TYPEERASE_PAGES;
	Erase.Page = g431_flash_page_for_address(G431_APP_SETTINGS_BASE);
	Erase.NbPages = 1;
	Erase.Banks = g431_flash_bank_for_address(G431_APP_SETTINGS_BASE);
	uint32_t PageError = 0xFFFFFFFFu;
	if ((HAL_FLASHEx_Erase(&Erase,&PageError) != HAL_OK) ||
			(PageError != 0xFFFFFFFFu)) {
		HAL_FLASH_Lock();
		return 0u;
	}

	for(uint32_t i = 0u;i < G431_TARGET_DOUBLEWORD;i++){
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
				G431_APP_SETTINGS_BASE + i * sizeof(uint64_t), data[i])
				!= HAL_OK) {
			HAL_FLASH_Lock();
			return 0u;
		}
	}

	HAL_FLASH_Lock();
	return 1u;
}

void flash_read(uint64_t* data){
	const void *flash_addr = (const void *)(uintptr_t)G431_APP_SETTINGS_BASE;

	memcpy(data, flash_addr, sizeof(FlashData));
}
