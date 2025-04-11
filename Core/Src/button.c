/*
 * button.c
 *
 *  Created on: Aug 1, 2024
 *      Author: Qinh
 */
#include "button.h"
#include "gpio.h"

static uint8_t button_init_state[8];
uint8_t extend_button;
uint8_t button;
extern vofa_test vofa1;

void button_init(){
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;

	for(uint8_t i =0;i<8;i++){
		button_init_state[i] = HAL_GPIO_ReadPin(GPIOA,(1 << i));
		if(button_init_state[i]){
			GPIO_InitStruct.Pin |= (1 << i);
		}
	}
	if(GPIO_InitStruct.Pin != 0){
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	}
}

void button_scan(){
	uint8_t tmp;
	button = 0;
	for(uint8_t i =0;i<8;i++){
		tmp = HAL_GPIO_ReadPin(GPIOA,(1 << i));
		if(tmp != button_init_state[i]){
			button |= (1 << i);
		}
	}
//	if(button == 0xff){
//		button = 0;
//	}
	extend_button = 0;
	if(!HAL_GPIO_ReadPin(GPIOC,GPIO_PIN_4)){
		extend_button = extend_button | 1;
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_0)){
		extend_button = extend_button | (1 << 1);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_1)){
		extend_button = extend_button | (1 << 2);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_2)){
		extend_button = extend_button | (1 << 3);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_10)){
		extend_button = extend_button | (1 << 4);
	}
	if(!HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_11)){
		extend_button = extend_button | (1 << 5);
	}

	return;
}
