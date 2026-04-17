/*
 * stack.c
 *
 *  Created on: Apr 25, 2025
 *      Author: Qinh
 */
#include "stack.h"
#include "capsense.h"
#include "button.h"
#include "string.h"

void stack_flow_touch(uint8_t* item) {
	memcpy(item,capsense_touch_status,34);
}

void stack_flow_button(uint8_t* item) {
	memcpy(item,button,2);
}
