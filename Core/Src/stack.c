/*
 * stack.c
 *
 *  Created on: Apr 25, 2025
 *      Author: Qinh
 */
#include "stack.h"
#include "capsense.h"
#include "button.h"
#include "flash.h"
#include "string.h"

uint8_t touch_stack_buffer[340];
uint8_t button_stack_buffer[20];
uint8_t touch_index = 0;
uint8_t button_index = 0;
extern FlashData Flash;

void stack_flow_touch(uint8_t* item) {
//	memcpy(item,&touch_stack_buffer[touch_index*34],34);
//	memcpy(&touch_stack_buffer[touch_index*34],capsense_touch_status,34);
//	if(touch_index == Flash.delay_setting[0]){
//		touch_index = 0;
//	}else{
//		touch_index ++;
//	}
//
	memcpy(item,capsense_touch_status,34);
}

void stack_flow_button(uint8_t* item) {
//	memcpy(item,&button_stack_buffer[button_index*2],2);
//	memcpy(&button_stack_buffer[button_index*34],button,2);
//	if(button_index == Flash.delay_setting[1]){
//		button_index = 0;
//	}else{
//		button_index ++;
//	}
	memcpy(item,button,2);
}
