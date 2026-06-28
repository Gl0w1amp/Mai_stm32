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

/* The settings image is erased one page and programmed as exactly 16
 * doublewords (sizeof(FlashData)/8). G431_APP_SETTINGS_BASE must stay
 * page-aligned and outside the application image for this to be safe. */
_Static_assert(sizeof(FlashData) == 16 * sizeof(uint64_t),
		"FlashData must be exactly 16 doublewords");

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

uint8_t flash_set_touch_threshold(uint8_t index, uint16_t value){
	if(index >= 34u){
		return 0u;
	}
	uint16_t prev = Flash.touch_threshold[index];
	Flash.touch_threshold[index] = value;
	uint8_t ok = flash_write(Flash.raw_flash);
	if(ok == 0u){
		Flash.touch_threshold[index] = prev;
	}
	return ok;
}

uint8_t flash_set_touch_sheet(const uint8_t *sheet){
	if(sheet == NULL){
		return 0u;
	}
	uint8_t prev[34];
	memcpy(prev, Flash.touch_sheet, 34);
	for(uint8_t i = 0; i < 34u; i++){
		Flash.touch_sheet[i] = sheet[i];
	}
	uint8_t ok = flash_write(Flash.raw_flash);
	if(ok == 0u){
		memcpy(Flash.touch_sheet, prev, 34);
	}
	return ok;
}

uint8_t flash_set_delay_setting(uint8_t index, uint8_t value){
	if(index >= 2u){
		return 0u;
	}
	uint8_t prev = Flash.delay_setting[index];
	Flash.delay_setting[index] = value;
	uint8_t ok = flash_write(Flash.raw_flash);
	if(ok == 0u){
		Flash.delay_setting[index] = prev;
	}
	return ok;
}

uint8_t flash_set_controller_role(uint8_t role){
	uint8_t prev = Flash.controller_role;
	Flash.controller_role = role;
	uint8_t ok = flash_write(Flash.raw_flash);
	if(ok == 0u){
		Flash.controller_role = prev;
	}
	return ok;
}

#define TOUCH_CHANNEL_COUNT 34u
#define TOUCH_THRESHOLD_DEFAULT 2000u
#define DELAY_SETTING_COUNT 2u
#define DELAY_SETTING_MAX 9u
#define CONFIG_VERSION 1

static const uint8_t touch_sheet_default[TOUCH_CHANNEL_COUNT] = {
		0,16,2,3,4,5,6,7,8,
		9,10,11,12,13,14,15,
		1,17,
		18,19,20,21,22,23,24,
		25,26,27,28,29,30,31,32,33
};

static void flash_load_defaults(void)
{
	for(uint8_t i = 0;i<TOUCH_CHANNEL_COUNT;i++){
		Flash.touch_threshold[i] = TOUCH_THRESHOLD_DEFAULT;
	}
	memcpy(Flash.touch_sheet, touch_sheet_default, TOUCH_CHANNEL_COUNT);
	Flash.delay_setting[0] = 0u;
	Flash.delay_setting[1] = 0u;
	Flash.controller_role = 1u;
	Flash.system_config = CONFIG_VERSION;
}

uint8_t flash_touch_sheet_valid(const uint8_t *sheet)
{
	if (sheet == NULL) {
		return 0u;
	}
	for(uint8_t i = 0;i<TOUCH_CHANNEL_COUNT;i++){
		if (sheet[i] >= TOUCH_CHANNEL_COUNT) {
			return 0u;
		}
	}
	return 1u;
}

uint8_t flash_config_sanitize(void)
{
	uint8_t changed = 0u;

	if(Flash.system_config != CONFIG_VERSION){
		flash_load_defaults();
		return flash_write(Flash.raw_flash);
	}

	if (flash_touch_sheet_valid(Flash.touch_sheet) == 0u) {
		memcpy(Flash.touch_sheet, touch_sheet_default, TOUCH_CHANNEL_COUNT);
		changed = 1u;
	}
	for(uint8_t i = 0;i<DELAY_SETTING_COUNT;i++){
		if(Flash.delay_setting[i] > DELAY_SETTING_MAX){
			Flash.delay_setting[i] = DELAY_SETTING_MAX;
			changed = 1u;
		}
	}
	if ((Flash.controller_role != 1u) && (Flash.controller_role != 2u)) {
		Flash.controller_role = 1u;
		changed = 1u;
	}

	if (changed != 0u) {
		return flash_write(Flash.raw_flash);
	}
	return 1u;
}
