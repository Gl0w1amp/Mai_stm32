/*
 * button.c
 *
 *  Created on: Aug 1, 2024
 *      Author: Qinh
 */
#include "button.h"
#include "gpio.h"
#include <stdbool.h>
//#include "adc.h"

static float button_init_state[8];
uint8_t button[2];
bool button_disable_flag = false;

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

//	HAL_ADCEx_Calibration_GetValue(&hadc1,ADC_SINGLE_ENDED);
//	HAL_ADCEx_Calibration_GetValue(&hadc2,ADC_SINGLE_ENDED);
//
//	for(uint8_t j = 0; j < 3;j++){
//		for(uint8_t i =0;i<4;i++){
//			HAL_ADC_Start(&hadc1);
//			HAL_ADC_Start(&hadc2);
//			HAL_ADC_PollForConversion(&hadc1, 10);
//			HAL_ADC_PollForConversion(&hadc2, 10);
//			button_init_state[2*i] += HAL_ADC_GetValue(&hadc1) / 3;
//			button_init_state[2*i+1] = HAL_ADC_GetValue(&hadc2) / 3;
//		}
//	}
////	for(uint8_t i = 0;i < 8;i++){
////		button_init_state[i] += 20;
////	}
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
//	float tmp[8]={0,0,0,0,0,0,0,0};
//	button = 0;
//	for(uint8_t j = 0; j < 10;j++){
//		for(uint8_t i =0;i<4;i++){
//			HAL_ADC_Start(&hadc1);
//			HAL_ADC_Start(&hadc2);
//			HAL_ADC_PollForConversion(&hadc1, 10);
//			HAL_ADC_PollForConversion(&hadc2, 10);
//			tmp[2*i] += HAL_ADC_GetValue(&hadc1)/10;
//			tmp[2*i+1] += HAL_ADC_GetValue(&hadc2)/10;
//		}
//	}
//	//CDC_Transmit_FS((uint8_t*)tmp, 8);
//	for(uint8_t i = 0;i < 8;i++){
//		if((button_init_state[i] < 2048) && (tmp[i] > button_init_state[i])){
//			button |= (1 << i);
//		}else if((button_init_state[i] > 2048) && (tmp[i] < button_init_state[i])){
//			button |= (1 << i);
//		}
//	}
//	if(button == 0xff){
//		button = 0;
//	}
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

	return;
}
