/*
 * capsense.c
 *
 *  Created on: Jan 8, 2025
 *      Author: Qinh
 */
#include "capsense.h"
#include "usart.h"
#include "string.h"
#include <math.h>
#include "usbd_cdc_acm_if.h"
#include "cmsis_os.h"
#include "flash.h"
#include "stdbool.h"

#define CAPSENSE_BASELINE_VARIANCE 3000
#define CAPSENSE_BASELINE_VARIANCE_A 1000
#define CAPSENSE_BASELINE_VARIANCE_B 600
#define CAPSENSE_BASELINE_VARIANCE_C 500
#define CAPSENSE_BASELINE_VARIANCE_D 800
#define CAPSENSE_BASELINE_VARIANCE_E 600

#define CAPSENSE_TOUCH_SETTLE_DURATION 12
#define CAPSENSE_HOLD_RELEASE_SWITCH_DURATION 24
#define CAPSENSE_HOLD_LOCK_DURATION 100
#define CAPSENSE_DURATION_B 1000
#define CAPSENSE_SHORT_RELEASE_NUMERATOR 9
#define CAPSENSE_SHORT_RELEASE_DENOMINATOR 10
#define CAPSENSE_HOLD_RELEASE_NUMERATOR 13
#define CAPSENSE_HOLD_RELEASE_DENOMINATOR 20
#define CAPSENSE_TOUCH_BASELINE_TRACK_NUMERATOR 1
#define CAPSENSE_TOUCH_BASELINE_TRACK_DENOMINATOR 20
#define CAPSENSE_TOUCH_BASELINE_MAX_RISE_NUMERATOR 1
#define CAPSENSE_TOUCH_BASELINE_MAX_RISE_DENOMINATOR 4

uint8_t uart_dma_buffer[128];

extern UART_HandleTypeDef huart4;
extern DMA_HandleTypeDef hdma_uart4_rx;
extern FlashData Flash;

packet_capsense_t Touch;
uint16_t capsense_raw_windows[10][34];
uint8_t capsense_raw_bet = 0;
uint16_t capsense_hold_duration[16] = {0};
uint16_t capsense_level[8] = {0};
uint16_t capsense_freeze[34];
uint16_t capsense_baseline[34];
uint16_t capsense_threshold[34];
uint8_t capsense_data_ready = 0;
uint8_t capsense_bit;
uint8_t capsense_touch_status[34];
uint8_t capsense_procotl_version = 0;
uint8_t capsense_checksum_last = 0;

typedef union{
    float raw_data_fl[2];
    uint8_t raw_data_u8[8];
}vofa;

vofa vofa1;
uint8_t debug_channel = 0;
extern uint8_t debug_flag;

static uint8_t capsense_channel_for_logical(uint8_t logical_index)
{
	return Flash.touch_sheet[logical_index];
}

static uint16_t capsense_release_threshold(uint16_t enter_threshold, uint16_t hold_duration)
{
	uint32_t numerator = CAPSENSE_SHORT_RELEASE_NUMERATOR;
	uint32_t denominator = CAPSENSE_SHORT_RELEASE_DENOMINATOR;
	uint32_t threshold;

	if (hold_duration >= CAPSENSE_HOLD_RELEASE_SWITCH_DURATION) {
		numerator = CAPSENSE_HOLD_RELEASE_NUMERATOR;
		denominator = CAPSENSE_HOLD_RELEASE_DENOMINATOR;
	}

	threshold = ((uint32_t) enter_threshold * numerator) / denominator;

	if (threshold == 0) {
		threshold = 1;
	}

	return (uint16_t) threshold;
}

static void capsense_update_touch_baseline(uint8_t channel, uint16_t raw, uint16_t enter_threshold)
{
	uint32_t baseline = capsense_baseline[channel];
	uint32_t tracked_baseline;
	uint32_t max_rise;
	uint32_t cap;

	tracked_baseline =
			(baseline * (CAPSENSE_TOUCH_BASELINE_TRACK_DENOMINATOR - CAPSENSE_TOUCH_BASELINE_TRACK_NUMERATOR))
			+ ((uint32_t) raw * CAPSENSE_TOUCH_BASELINE_TRACK_NUMERATOR);
	tracked_baseline /= CAPSENSE_TOUCH_BASELINE_TRACK_DENOMINATOR;

	max_rise = ((uint32_t) enter_threshold * CAPSENSE_TOUCH_BASELINE_MAX_RISE_NUMERATOR)
			/ CAPSENSE_TOUCH_BASELINE_MAX_RISE_DENOMINATOR;
	if (max_rise == 0) {
		max_rise = 1;
	}

	cap = (uint32_t) capsense_freeze[channel] + max_rise;
	if (tracked_baseline > cap) {
		tracked_baseline = cap;
	}

	capsense_baseline[channel] = (uint16_t) tracked_baseline;
}

static void capsense_update_idle_baseline(uint8_t channel, uint16_t raw, uint16_t variance_limit)
{
	if (capsense_baseline[channel] + variance_limit > raw) {
		float baseline = (capsense_baseline[channel] * 0.9f) + (raw * 0.1f);
		capsense_baseline[channel] = baseline > 60000 ? 60000 : baseline;
	}
}

static void capsense_process_hold_block(uint8_t logical_start, uint8_t hold_offset)
{
	for(uint8_t logical = logical_start; logical < logical_start + 8; logical++){
		uint8_t hold_index = hold_offset + (logical - logical_start);
		uint8_t channel = capsense_channel_for_logical(logical);
		uint16_t raw = Touch.channel_raw[channel];
		uint16_t enter_threshold = Flash.touch_threshold[logical];
		uint16_t release_threshold = 0;
		int variance;

		if(capsense_baseline[channel] == 0){
			capsense_baseline[channel] = raw;
		}
		if(capsense_hold_duration[hold_index] == 0){
			capsense_freeze[channel] = capsense_baseline[channel];
		}else if(capsense_hold_duration[hold_index] >= CAPSENSE_HOLD_LOCK_DURATION){
			capsense_baseline[channel] = capsense_freeze[channel];
		}

		variance = raw - capsense_baseline[channel];
		if(capsense_touch_status[logical]){
			if((capsense_hold_duration[hold_index] > 0)
					&& (capsense_hold_duration[hold_index] < CAPSENSE_TOUCH_SETTLE_DURATION)
					&& (raw < 0xFF00)){
				capsense_update_touch_baseline(channel, raw, enter_threshold);
				capsense_freeze[channel] = capsense_baseline[channel];
				variance = raw - capsense_baseline[channel];
			}

			release_threshold = capsense_release_threshold(enter_threshold, capsense_hold_duration[hold_index]);
			if((variance >= release_threshold) || (raw >= 0xFF00)){
				if(capsense_hold_duration[hold_index] < 10000){
					capsense_hold_duration[hold_index] ++;
				}
			} else {
				capsense_touch_status[logical] = 0;
				capsense_hold_duration[hold_index] = 0;
				capsense_update_idle_baseline(channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
			}
		} else if((variance > enter_threshold) || (raw >= 0xFF00)){
			capsense_touch_status[logical] = 1;
			if(capsense_hold_duration[hold_index] < 10000){
				capsense_hold_duration[hold_index] ++;
			}
		} else {
			capsense_touch_status[logical] = 0;
			capsense_hold_duration[hold_index] = 0;
			capsense_update_idle_baseline(channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
		}
	}
}

static void capsense_process_standard_block(uint8_t logical_start, uint8_t count, uint16_t variance_limit)
{
	for(uint8_t logical = logical_start; logical < logical_start + count; logical++){
		uint8_t channel = capsense_channel_for_logical(logical);
		uint16_t raw = Touch.channel_raw[channel];
		int variance;

		if(capsense_baseline[channel] == 0){
			capsense_baseline[channel] = raw;
		}
		if(capsense_baseline[channel] + variance_limit > raw){
			float baseline = (capsense_baseline[channel] * 0.8f) + (raw * 0.2f);
			capsense_baseline[channel] = baseline;
		}
		variance = raw - capsense_baseline[channel];
		if((variance > Flash.touch_threshold[logical]) || (raw >= 0xFF00)){
			capsense_touch_status[logical] = 1;
		}
		else{
			capsense_touch_status[logical] = 0;
		}
	}
}

bool check_checksum(uint8_t* data){
	uint8_t checksum = 0;
	for(uint8_t i = 0;i<69;i++){
		checksum += data[i];
	}
	if(checksum == data[69]){
		return true;
	}else{
		return false;
	}
}
uint8_t checksum = 0;

static uint8_t capsense_accept_packet(const uint8_t *data)
{
	memcpy(&Touch.data[0], data + 1, 68);
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 1);
	if(capsense_procotl_version == 0){
		capsense_procotl_version = 1;
	}
	capsense_data_ready = 1;
	return 1;
}

//static inline void UART_ClearIdle(UART_HandleTypeDef *huart)
//{
//	//dont use on stm32F1/F2/F3/F4,them has usart v1
//	huart->Instance->ICR = USART_ICR_IDLECF;
//}


//void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
//{
//    if (huart->Instance == UART4)
//    {
////    	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_15,1);
////		HAL_UART_DMAStop(&huart4);
//		uint8_t len = 70 - __HAL_DMA_GET_COUNTER(huart4.hdmarx);
//		if(len ==70 ){
//			uint8_t ret = 0;
//			for(uint8_t i = 0;i<70;i++){
//				if(uart_dma_buffer[i] == 0){
//					ret++;
//				}else{
//					break;
//				}
//			}
//			if(ret >= 69){
//				uint8_t tes5 = 0x47;
//						CDC_Transmit(0,&tes5,1);
//				goto end;
//			}
//			if(!capsense_data_proc(uart_dma_buffer)){
//				if(!capsense_data_proc_legacy(uart_dma_buffer)){
//
//				}
//			}
//		}else{
//	    	uint8_t tes5[71] = {0x17};
//	    				memcpy(tes5+1,uart_dma_buffer,70);
//	    						CDC_Transmit(0,&tes5,71);
//		}
//	end:
////		UART_ClearIdle(&huart4);
//		HAL_UART_Receive_DMA(&huart4,uart_dma_buffer,70);
//		return;
//    }
//}
//
//void Touch_UART_IDLE_Handler(){
//	UART_ClearIdle(&huart4);
//	HAL_UART_Receive_DMA(&huart4,uart_dma_buffer,70);
//	__HAL_UART_DISABLE_IT(&huart4, UART_IT_IDLE);
//}

bool capsense_data_proc(uint8_t *uart_dma_buffer){
	uint8_t strict_checksum;
	uint8_t rolling_checksum;

	if((capsense_procotl_version != 0) && (capsense_procotl_version != 1)){
		return false;
	}
    if(uart_dma_buffer[0] == 0){
		strict_checksum = 0;
		for(uint8_t i = 0;i<69;i++){
			strict_checksum += uart_dma_buffer[i];
		}
		if(strict_checksum == uart_dma_buffer[69]){
			capsense_checksum_last = uart_dma_buffer[69];
			return capsense_accept_packet(uart_dma_buffer);
		}

		/* Compatibility path for older PSoC firmware that accumulates checksum
		 * across frames instead of resetting it each packet.
		 */
		rolling_checksum = capsense_checksum_last + strict_checksum;
		if(rolling_checksum == uart_dma_buffer[69]){
			capsense_checksum_last = uart_dma_buffer[69];
			return capsense_accept_packet(uart_dma_buffer);
		}
    }
    return false;
}

bool capsense_data_proc_legacy(uint8_t *uart_dma_buffer){
	if((capsense_procotl_version != 0) && (capsense_procotl_version != 2)){
		return false;
	}
    if((uart_dma_buffer[0] == 0 ) && (uart_dma_buffer[1] == 0)){
		memcpy(&Touch.data[0],uart_dma_buffer+2,68);
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 1);
		if(capsense_procotl_version == 0){
			capsense_procotl_version = 2;
		}
		capsense_data_ready = 1;
		return true;
    }
    return false;
}
void Boot_Buttom_IRQHandler(){
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_3,1);
	for(uint8_t i = 0;i<34;i++){
		capsense_baseline[i] = 0;
		capsense_freeze[i] = 0;

		Touch.channel_raw[i] = 0;
		capsense_touch_status[i] = 0;
	}
	for(uint8_t i = 0;i<16;i++){
		capsense_hold_duration[i] = 0;
	}
	capsense_checksum_last = 0;
	HAL_GPIO_WritePin(GPIOB,GPIO_PIN_3,0);
}

void capsense_init(){
	while(HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart_dma_buffer, 128) != HAL_OK){

	}
	__HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
	capsense_checksum_last = 0;
	osDelay(100);
	for(uint8_t i = 0;i<34;i++){
		capsense_baseline[i] = Touch.channel_raw[i];
		capsense_freeze[i] = Touch.channel_raw[i];
	}
}

//void capsense_baseline_updata(uint8_t channel){
//	float average = 0;
//	for(uint8_t k = 0;k < 10;k++){
//		average += capsense_raw_windows[k][channel]/10;
//	}
//	float variance = 0;
//	for (uint8_t k = 0;k < 10;k++) {
//		variance += powf(capsense_raw_windows[k][channel] - average, 2);
//	}
//	variance /= 10;
//	if(variance < CAPSENSE_BASELINE_VARIANCE){
//		capsense_baseline[channel] = average;
//	}
//}

void capsense_check(){
	capsense_process_hold_block(0, 0);
	capsense_process_standard_block(8, 8, CAPSENSE_BASELINE_VARIANCE_B);
	capsense_process_standard_block(16, 2, CAPSENSE_BASELINE_VARIANCE_C);
	capsense_process_hold_block(18, 8);
	capsense_process_standard_block(26, 8, CAPSENSE_BASELINE_VARIANCE_E);

//	//BLOCK A
//	for(uint8_t i = 0;i<8;i++){
//		if(capsense_duration[i] > 250){
//			capsense_baseline[i] = capsense_orinigal[i];
//		}
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//			if(capsense_duration[i] < 65535){
//				capsense_duration[i] ++;
//			}
//		}else{
//			capsense_touch_status[i] = 0;
//			capsense_duration[i] = 0;
//		}
//		if((Touch.channel_raw[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < 0xFFF0) && capsense_duration[i] <= 250){
//			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.85) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.15);
//			if(capsense_touch_status[i]){
//				if(baseline + Flash.touch_threshold[i]+ CAPSENSE_BASELINE_VARIANCE < Touch.channel_raw[Flash.touch_sheet[i]]){
//					capsense_baseline[Flash.touch_sheet[i]] = baseline;
//				}
//			}else{
//				capsense_baseline[Flash.touch_sheet[i]] = baseline;
//			}
//		}
//	}
//	//BLOCK B
//	for(uint8_t i = 8;i<16;i++){
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//		}else{
//			capsense_touch_status[i] = 0;
//		}
//	}
//	//BLOCK C
//	for(uint8_t i = 16;i<18;i++){
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//		}else{
//			capsense_touch_status[i] = 0;
//		}
//	}
//	//BLOCK D
//	for(uint8_t i = 18;i<26;i++){
//		if(capsense_duration[i] > 250){
//			capsense_baseline[i] = capsense_orinigal[i];
//		}
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//			if(capsense_duration[i] < 65535){
//				capsense_duration[i] ++;
//			}
//		}else{
//			capsense_touch_status[i] = 0;
//			capsense_duration[i] = 0;
//		}
//		if((Touch.channel_raw[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < 0xFFF0) && capsense_duration[i] <= 250){
//			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.85) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.15);
//			if(capsense_touch_status[i]){
//				if(baseline + Flash.touch_threshold[i]+ CAPSENSE_BASELINE_VARIANCE < Touch.channel_raw[Flash.touch_sheet[i]]){
//					capsense_baseline[Flash.touch_sheet[i]] = baseline;
//				}
//			}else{
//				capsense_baseline[Flash.touch_sheet[i]] = baseline;
//			}
//		}
//	}
//	//BLOCK E
//	for(uint8_t i = 26;i<34;i++){
//		if((capsense_baseline[Flash.touch_sheet[i]] + Flash.touch_threshold[i] < Touch.channel_raw[Flash.touch_sheet[i]]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
//			capsense_touch_status[i] = 1;
//		}else{
//			capsense_touch_status[i] = 0;
//		}
//	}
//	capsense_data_ready = 0;



	if(debug_flag){
		vofa1.raw_data_fl[0] = Touch.channel_raw[debug_channel];
	//	vofa1.raw_data_fl[1] = capsense_baseline[debug_channel];
		vofa1.raw_data_fl[1] = capsense_baseline[debug_channel] + Flash.touch_threshold[debug_channel];
		static uint8_t tmp[12] = {0,0,0,0,0,0,0,0,0,0,0x80,0x7f};
		memcpy(tmp,vofa1.raw_data_u8,8);
		CDC_Transmit(0, tmp,12);
	}
}
