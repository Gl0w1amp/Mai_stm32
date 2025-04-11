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

#define G431_FLASH_BASE        ((uint32_t)0x08000000)
#define G431_FLASH_SIZE        0x20000
#define G431_FLASH_PAGE_SIZE   0x800
#define G431_TARGET_PAGE       63
#define G431_TARGET_DOUBLEWORD 16 //预计使用16个双字存储全部信息

FlashData Flash;

void flash_write(uint64_t* data){
	HAL_FLASH_Unlock(); //flash解锁

	FLASH_EraseInitTypeDef Erase;
	Erase.TypeErase = FLASH_TYPEERASE_PAGES;
	Erase.Page = G431_TARGET_PAGE;
	Erase.NbPages = 1;
	Erase.Banks = FLASH_BANK_1;
	uint32_t PageError = 0;
	HAL_FLASHEx_Erase(&Erase,&PageError);
	//flash擦除
	for(uint8_t i = 0;i < G431_TARGET_DOUBLEWORD;i++){
		while(
				HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
				G431_FLASH_BASE + G431_FLASH_PAGE_SIZE * G431_TARGET_PAGE + i * 8 , data[i])
		!= HAL_OK);
	}

	HAL_FLASH_Lock();
}

void flash_read(uint64_t* data){
	memcpy(data, G431_FLASH_BASE + G431_FLASH_PAGE_SIZE * G431_TARGET_PAGE , 128);
}

