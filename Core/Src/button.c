/*
 * button.c
 *
 *  Created on: Aug 1, 2024
 *      Author: Qinh
 */
#include "button.h"
#include "gpio.h"
#include <stdbool.h>

static float button_init_state[8];
uint8_t button[2];
bool button_disable_flag = false;
uint8_t keyboard_sheet[14] = {
	0x1A,0x08,0x07,0x06,0x1B,0x1D,0x04,0x14,0x3A,0x3B,0x3C,0x28,0x3E,0x3F
};

void button_init(){
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	uint8_t button_flag = 0;
	for(uint8_t i =0;i<8;i++){
		button_init_state[i] = HAL_GPIO_ReadPin(GPIOA,(1 << i));
		if(button_init_state[i]){
			GPIO_InitStruct.Pin |= (1 << i);
		}
	}
	if(GPIO_InitStruct.Pin != 0){
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
		osDelay(10);
	}

	button_flag = 0;
	for(uint8_t i =0;i<8;i++){
		button_flag |= HAL_GPIO_ReadPin(GPIOA,(1 << i));
	}
	if((button_flag == 0) && (GPIO_InitStruct.Pin == 0xff)){
		button_disable_flag = true;
	}
}

void button_scan(){
	uint8_t tmp;
	button[0] = 0;
	if(!button_disable_flag){
		for(uint8_t i =0;i<8;i++){
			tmp = HAL_GPIO_ReadPin(GPIOA,(1 << i));
			if(tmp != button_init_state[i]){
				button[0] |= (1 << i);
			}
		}
	}
	button[1] = 0;
	if(!HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_4)){
		button[1] = button[1] | 1;
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_0)){
		button[1] = button[1] | (1 << 1);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_1)){
		button[1] = button[1] | (1 << 2);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_2)){
		button[1] = button[1] | (1 << 3);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_10)){
		button[1] = button[1] | (1 << 4);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_11)){
		button[1] = button[1] | (1 << 5);
	}

	if(button[0] || button[1]){
		HAL_GPIO_WritePin(GPIOB,GPIO_PIN_15,1);
	}else{
		HAL_GPIO_WritePin(GPIOB,GPIO_PIN_15,0);
	}
	return;
}
