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
#define CAPSENSE_BASELINE_COOLDOWN_FRAMES 16
#define CAPSENSE_BASELINE_RISE_MARGIN 128
#define CAPSENSE_BASELINE_RISE_NUMERATOR 1
#define CAPSENSE_BASELINE_RISE_DENOMINATOR 50
#define CAPSENSE_BASELINE_FALL_NUMERATOR 1
#define CAPSENSE_BASELINE_FALL_DENOMINATOR 5

#define CAPSENSE_TOUCH_ENTER_CONFIRM_SAMPLES 2
#define CAPSENSE_TOUCH_RELEASE_CONFIRM_SAMPLES 2
#define CAPSENSE_POST_RELEASE_INHIBIT_FRAMES 2
#define CAPSENSE_SHORT_RELEASE_NUMERATOR 1
#define CAPSENSE_SHORT_RELEASE_DENOMINATOR 2
#define CAPSENSE_DYNAMIC_FOLLOW_RISE_NUMERATOR 1
#define CAPSENSE_DYNAMIC_FOLLOW_RISE_DENOMINATOR 2
#define CAPSENSE_DYNAMIC_FOLLOW_FALL_NUMERATOR 1
#define CAPSENSE_DYNAMIC_FOLLOW_FALL_DENOMINATOR 8
#define CAPSENSE_DYNAMIC_FOLLOW_CAP_NUMERATOR 7
#define CAPSENSE_DYNAMIC_FOLLOW_CAP_DENOMINATOR 8
#define CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT 128
#define CAPSENSE_AUTO_THRESHOLD_FRAME_TIMEOUT_MS 20
#define CAPSENSE_AUTO_THRESHOLD_TOTAL_TIMEOUT_MS 5000
#define CAPSENSE_AUTO_THRESHOLD_STABLE_FRAMES 16
#define CAPSENSE_AUTO_THRESHOLD_STEP_MAX 192
#define CAPSENSE_AUTO_THRESHOLD_BASELINE_DELTA_MAX 320
#define CAPSENSE_AUTO_THRESHOLD_TRIM_PERCENT 5
#define CAPSENSE_AUTO_THRESHOLD_TRIM_MULTIPLIER 4
#define CAPSENSE_AUTO_THRESHOLD_MIN 180
#define CAPSENSE_AUTO_THRESHOLD_MAX 4000
#define CAPSENSE_AUTO_THRESHOLD_P2P_CAP_MULTIPLIER 2
#define CAPSENSE_AUTO_THRESHOLD_POS_MULTIPLIER 4

typedef enum {
	CAPSENSE_HOLD_STATE_IDLE = 0,
	CAPSENSE_HOLD_STATE_ACTIVE = 1,
	CAPSENSE_HOLD_STATE_RELEASE_CONFIRM = 2
} capsense_hold_state_t;

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
uint16_t capsense_hold_prev_raw[16] = {0};
uint16_t capsense_hold_peak_envelope[16] = {0};
uint16_t capsense_hold_release_level[16] = {0};
uint8_t capsense_hold_baseline_cooldown[16] = {0};
uint8_t capsense_data_ready = 0;
uint8_t capsense_bit;
uint8_t capsense_touch_status[34];
uint8_t capsense_hold_enter_confirm[16] = {0};
uint8_t capsense_hold_release_confirm[16] = {0};
uint8_t capsense_hold_rearm_confirm[16] = {0};
uint8_t capsense_hold_state[16] = {0};
uint8_t capsense_procotl_version = 0;
uint8_t capsense_checksum_last = 0;
volatile uint32_t capsense_frame_counter = 0;

typedef union{
    float raw_data_fl[6];
    uint8_t raw_data_u8[24];
}vofa;

vofa vofa1;
uint8_t debug_channel = 0;
extern uint8_t debug_flag;
static uint16_t capsense_auto_threshold_samples[34][CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT];
static uint16_t capsense_auto_threshold_sorted[CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT];

static uint8_t capsense_channel_for_logical(uint8_t logical_index)
{
	return Flash.touch_sheet[logical_index];
}

static uint16_t capsense_release_threshold(uint16_t enter_threshold)
{
	uint32_t numerator = CAPSENSE_SHORT_RELEASE_NUMERATOR;
	uint32_t denominator = CAPSENSE_SHORT_RELEASE_DENOMINATOR;
	uint32_t threshold;

	threshold = ((uint32_t) enter_threshold * numerator) / denominator;

	if (threshold == 0) {
		threshold = 1;
	}

	return (uint16_t) threshold;
}

static uint16_t capsense_dynamic_release_threshold(uint16_t enter_threshold, uint16_t follow_level)
{
	uint32_t threshold = capsense_release_threshold(enter_threshold);

	if (follow_level > threshold) {
		threshold = follow_level;
	}
	if (threshold == 0) {
		threshold = 1;
	}

	return (uint16_t) threshold;
}

static uint16_t capsense_follow_target(uint16_t enter_threshold, uint16_t delta)
{
	uint32_t target = capsense_release_threshold(enter_threshold);
	uint32_t capped_delta =
			((uint32_t) delta * CAPSENSE_DYNAMIC_FOLLOW_CAP_NUMERATOR) /
			CAPSENSE_DYNAMIC_FOLLOW_CAP_DENOMINATOR;

	if (capped_delta > target) {
		target = capped_delta;
	}

	return (uint16_t) target;
}

static uint16_t capsense_update_follow_level(uint16_t follow_level, uint16_t target_level)
{
	uint32_t updated = follow_level;

	if (target_level > follow_level) {
		uint32_t step = ((uint32_t) (target_level - follow_level) *
				CAPSENSE_DYNAMIC_FOLLOW_RISE_NUMERATOR +
				CAPSENSE_DYNAMIC_FOLLOW_RISE_DENOMINATOR - 1) /
				CAPSENSE_DYNAMIC_FOLLOW_RISE_DENOMINATOR;
		updated += step;
	} else if (target_level < follow_level) {
		uint32_t step = ((uint32_t) (follow_level - target_level) *
				CAPSENSE_DYNAMIC_FOLLOW_FALL_NUMERATOR +
				CAPSENSE_DYNAMIC_FOLLOW_FALL_DENOMINATOR - 1) /
				CAPSENSE_DYNAMIC_FOLLOW_FALL_DENOMINATOR;
		if (step > updated) {
			updated = 0;
		} else {
			updated -= step;
		}
	} else {
		updated = target_level;
	}

	return (uint16_t) updated;
}

static uint8_t capsense_hold_index_for_logical(uint8_t logical_index)
{
	if (logical_index < 8) {
		return logical_index;
	}
	if ((logical_index >= 18) && (logical_index < 26)) {
		return (uint8_t) (logical_index - 10);
	}
	return 0xFF;
}

static void capsense_update_idle_baseline(uint8_t hold_index, uint8_t channel, uint16_t raw, uint16_t variance_limit);

static uint16_t capsense_debug_enter_line(uint8_t logical_index)
{
	uint8_t channel = capsense_channel_for_logical(logical_index);
	uint8_t hold_index = capsense_hold_index_for_logical(logical_index);
	uint16_t reference = capsense_baseline[channel];

	if ((hold_index != 0xFF) && (capsense_hold_state[hold_index] != CAPSENSE_HOLD_STATE_IDLE)) {
		reference = capsense_freeze[channel];
	}

	return reference + Flash.touch_threshold[logical_index];
}

static uint16_t capsense_debug_release_line(uint8_t logical_index)
{
	uint8_t channel = capsense_channel_for_logical(logical_index);
	uint16_t enter_threshold = Flash.touch_threshold[logical_index];
	uint8_t hold_index = capsense_hold_index_for_logical(logical_index);
	uint16_t release_threshold = capsense_release_threshold(enter_threshold);
	uint16_t reference = capsense_baseline[channel];

	if ((hold_index != 0xFF) && (capsense_hold_state[hold_index] != CAPSENSE_HOLD_STATE_IDLE)) {
		reference = capsense_freeze[channel];
		release_threshold = capsense_hold_release_level[hold_index];
	}

	return reference + release_threshold;
}

static void capsense_reset_hold_contact(uint8_t logical, uint8_t hold_index, uint8_t channel, uint16_t raw,
		uint16_t variance_limit)
{
	(void) variance_limit;
	capsense_touch_status[logical] = 0;
	capsense_hold_duration[hold_index] = 0;
	capsense_hold_enter_confirm[hold_index] = 0;
	capsense_hold_release_confirm[hold_index] = 0;
	capsense_hold_rearm_confirm[hold_index] = CAPSENSE_POST_RELEASE_INHIBIT_FRAMES;
	capsense_hold_prev_raw[hold_index] = raw;
	capsense_hold_baseline_cooldown[hold_index] = CAPSENSE_BASELINE_COOLDOWN_FRAMES;
	capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_IDLE;
	capsense_freeze[channel] = capsense_baseline[channel];
}

static void capsense_start_hold_contact(uint8_t logical, uint8_t hold_index, uint8_t channel, uint16_t raw,
		uint16_t enter_threshold)
{
	uint16_t delta = raw > capsense_baseline[channel] ? (uint16_t) (raw - capsense_baseline[channel]) : 0;
	uint16_t follow_target = capsense_follow_target(enter_threshold, delta);

	capsense_touch_status[logical] = 1;
	capsense_hold_duration[hold_index] = 1;
	capsense_hold_enter_confirm[hold_index] = 0;
	capsense_hold_release_confirm[hold_index] = 0;
	capsense_hold_rearm_confirm[hold_index] = 0;
	capsense_hold_prev_raw[hold_index] = raw;
	capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_ACTIVE;
	capsense_freeze[channel] = capsense_baseline[channel];
	capsense_hold_peak_envelope[hold_index] = follow_target;
	capsense_hold_release_level[hold_index] = capsense_dynamic_release_threshold(enter_threshold, follow_target);
	capsense_hold_baseline_cooldown[hold_index] = CAPSENSE_BASELINE_COOLDOWN_FRAMES;
}

static void capsense_update_idle_baseline(uint8_t hold_index, uint8_t channel, uint16_t raw, uint16_t variance_limit)
{
	uint16_t baseline = capsense_baseline[channel];
	uint32_t updated = baseline;

	if (raw <= baseline) {
		updated = ((uint32_t) baseline * (CAPSENSE_BASELINE_FALL_DENOMINATOR - CAPSENSE_BASELINE_FALL_NUMERATOR)) +
				((uint32_t) raw * CAPSENSE_BASELINE_FALL_NUMERATOR);
		updated /= CAPSENSE_BASELINE_FALL_DENOMINATOR;
	} else {
		if (hold_index < 16) {
			if (capsense_hold_baseline_cooldown[hold_index] > 0) {
				capsense_hold_baseline_cooldown[hold_index]--;
				return;
			}
		}
		if ((((uint32_t) baseline + variance_limit) <= raw) ||
				(((uint32_t) baseline + CAPSENSE_BASELINE_RISE_MARGIN) < raw)) {
			return;
		}
		updated = ((uint32_t) baseline * (CAPSENSE_BASELINE_RISE_DENOMINATOR - CAPSENSE_BASELINE_RISE_NUMERATOR)) +
				((uint32_t) raw * CAPSENSE_BASELINE_RISE_NUMERATOR);
		updated /= CAPSENSE_BASELINE_RISE_DENOMINATOR;
	}

	capsense_baseline[channel] = updated > 60000 ? 60000 : (uint16_t) updated;
}

static void capsense_process_hold_block(uint8_t logical_start, uint8_t hold_offset)
{
	for(uint8_t logical = logical_start; logical < logical_start + 8; logical++){
		uint8_t hold_index = hold_offset + (logical - logical_start);
		uint8_t channel = capsense_channel_for_logical(logical);
		uint16_t raw = Touch.channel_raw[channel];
		uint16_t enter_threshold = Flash.touch_threshold[logical];
		uint16_t static_release_threshold = capsense_release_threshold(enter_threshold);
		capsense_hold_state_t state = (capsense_hold_state_t) capsense_hold_state[hold_index];
		uint16_t reference;
		uint16_t delta_freeze;
		uint16_t release_threshold;
		int variance_idle;

		if(capsense_baseline[channel] == 0){
			capsense_baseline[channel] = raw;
		}
		variance_idle = (int) raw - (int) capsense_baseline[channel];
		capsense_hold_release_level[hold_index] = static_release_threshold;

		if(state == CAPSENSE_HOLD_STATE_IDLE){
			capsense_freeze[channel] = capsense_baseline[channel];
			if(capsense_hold_peak_envelope[hold_index] > 0){
				uint16_t follow_target = capsense_follow_target(enter_threshold, 0);
				capsense_hold_peak_envelope[hold_index] =
						capsense_update_follow_level(capsense_hold_peak_envelope[hold_index], follow_target);
			}
			if(capsense_hold_rearm_confirm[hold_index] > 0){
				capsense_hold_rearm_confirm[hold_index]--;
				capsense_hold_enter_confirm[hold_index] = 0;
				capsense_touch_status[logical] = 0;
				capsense_hold_duration[hold_index] = 0;
				capsense_hold_release_confirm[hold_index] = 0;
				capsense_hold_prev_raw[hold_index] = raw;
				capsense_hold_release_level[hold_index] =
						capsense_dynamic_release_threshold(enter_threshold, capsense_hold_peak_envelope[hold_index]);
				capsense_update_idle_baseline(hold_index, channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
				capsense_freeze[channel] = capsense_baseline[channel];
				continue;
			}
			if((variance_idle > (int) enter_threshold) || (raw >= 0xFF00)){
				if(capsense_hold_enter_confirm[hold_index] < 0xFF){
					capsense_hold_enter_confirm[hold_index]++;
				}
				if(capsense_hold_enter_confirm[hold_index] >= CAPSENSE_TOUCH_ENTER_CONFIRM_SAMPLES){
					capsense_start_hold_contact(logical, hold_index, channel, raw, enter_threshold);
				}else{
					capsense_touch_status[logical] = 0;
					capsense_hold_duration[hold_index] = 0;
					capsense_hold_release_confirm[hold_index] = 0;
					capsense_hold_rearm_confirm[hold_index] = 0;
					capsense_hold_prev_raw[hold_index] = raw;
				}
			}else{
				capsense_hold_enter_confirm[hold_index] = 0;
				capsense_touch_status[logical] = 0;
				capsense_hold_duration[hold_index] = 0;
				capsense_hold_release_confirm[hold_index] = 0;
				capsense_hold_rearm_confirm[hold_index] = 0;
				capsense_hold_prev_raw[hold_index] = raw;
				capsense_hold_release_level[hold_index] =
						capsense_dynamic_release_threshold(enter_threshold, capsense_hold_peak_envelope[hold_index]);
				capsense_update_idle_baseline(hold_index, channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
				capsense_freeze[channel] = capsense_baseline[channel];
			}
			continue;
		}

		reference = capsense_freeze[channel];
		delta_freeze = raw > reference ? (uint16_t) (raw - reference) : 0;
		{
			uint16_t follow_target = capsense_follow_target(enter_threshold, delta_freeze);
		capsense_hold_peak_envelope[hold_index] =
				capsense_update_follow_level(capsense_hold_peak_envelope[hold_index], follow_target);
		}
		release_threshold =
				capsense_dynamic_release_threshold(enter_threshold, capsense_hold_peak_envelope[hold_index]);
		capsense_hold_release_level[hold_index] = release_threshold;

		if((delta_freeze < release_threshold) && (raw < 0xFF00)){
			if(capsense_hold_release_confirm[hold_index] < 0xFF){
				capsense_hold_release_confirm[hold_index]++;
			}
			capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_RELEASE_CONFIRM;
			capsense_hold_prev_raw[hold_index] = raw;
			if(capsense_hold_release_confirm[hold_index] >= CAPSENSE_TOUCH_RELEASE_CONFIRM_SAMPLES){
				capsense_reset_hold_contact(logical, hold_index, channel, raw, CAPSENSE_BASELINE_VARIANCE_A);
			}else{
				capsense_touch_status[logical] = 1;
				if(capsense_hold_duration[hold_index] < 10000){
					capsense_hold_duration[hold_index]++;
				}
			}
			continue;
		}

		capsense_hold_enter_confirm[hold_index] = 0;
		capsense_hold_release_confirm[hold_index] = 0;
		capsense_hold_rearm_confirm[hold_index] = 0;
		capsense_touch_status[logical] = 1;
		capsense_hold_state[hold_index] = CAPSENSE_HOLD_STATE_ACTIVE;
		capsense_hold_prev_raw[hold_index] = raw;
		if(capsense_hold_duration[hold_index] < 10000){
			capsense_hold_duration[hold_index]++;
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
	capsense_frame_counter++;
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
		capsense_frame_counter++;
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
		capsense_hold_enter_confirm[i] = 0;
		capsense_hold_release_confirm[i] = 0;
		capsense_hold_rearm_confirm[i] = 0;
		capsense_hold_baseline_cooldown[i] = 0;
		capsense_hold_state[i] = CAPSENSE_HOLD_STATE_IDLE;
		capsense_hold_prev_raw[i] = 0;
		capsense_hold_peak_envelope[i] = 0;
		capsense_hold_release_level[i] = 0;
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
	for(uint8_t i = 0;i<16;i++){
		capsense_hold_baseline_cooldown[i] = 0;
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
		uint8_t logical = debug_channel < 34 ? debug_channel : 0;
		uint8_t channel = capsense_channel_for_logical(logical);
		uint8_t hold_index =
				(logical < 8) ? logical :
				((logical >= 18) && (logical < 26)) ? (logical - 10) : 0xFF;
		float hold_state = -1.0f;
		float hold_duration = 0.0f;
		static uint8_t tmp[28] = {
				0,0,0,0,0,0,0,0,0,0,
				0,0,0,0,0,0,0,0,0,0,
				0,0,0,0,0,0,0x80,0x7f
		};

		if (hold_index != 0xFF) {
			hold_state = (float) capsense_hold_state[hold_index];
			hold_duration = (float) capsense_hold_duration[hold_index];
		}

		vofa1.raw_data_fl[0] = Touch.channel_raw[channel];
		vofa1.raw_data_fl[1] = capsense_debug_enter_line(logical);
		vofa1.raw_data_fl[2] = capsense_debug_release_line(logical);
		vofa1.raw_data_fl[3] = hold_state;
		vofa1.raw_data_fl[4] = capsense_touch_status[logical] ? 1.0f : 0.0f;
		vofa1.raw_data_fl[5] = hold_duration;
		memcpy(tmp,vofa1.raw_data_u8,24);
		CDC_Transmit(0, tmp,28);
	}
}

uint8_t capsense_auto_calibrate_thresholds(uint16_t *thresholds_out, uint16_t *min_threshold_out, uint16_t *max_threshold_out)
{
	uint16_t min_raw[34];
	uint16_t max_raw[34];
	uint16_t max_positive_delta[34];
	uint16_t raw_frame[34];
	uint16_t positive_delta_frame[34];
	uint16_t prev_raw[34];
	uint16_t threshold_min = 0xFFFF;
	uint16_t threshold_max = 0;
	uint32_t last_frame_counter;
	uint32_t start_tick;
	uint16_t sample_count = 0;
	uint8_t stable_frames = 0;
	uint8_t prev_raw_valid = 0;
	const uint16_t trim_low_index = (CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT * CAPSENSE_AUTO_THRESHOLD_TRIM_PERCENT) / 100;
	const uint16_t trim_high_index = CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT - 1 - trim_low_index;

	if (thresholds_out == NULL) {
		return 0;
	}
	last_frame_counter = capsense_frame_counter;
	start_tick = HAL_GetTick();

	while (sample_count < CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT) {
		uint32_t timeout_ms = CAPSENSE_AUTO_THRESHOLD_FRAME_TIMEOUT_MS;
		uint8_t frame_valid = 1;

		while (capsense_frame_counter == last_frame_counter) {
			if ((timeout_ms == 0) || ((HAL_GetTick() - start_tick) >= CAPSENSE_AUTO_THRESHOLD_TOTAL_TIMEOUT_MS)) {
				return 0;
			}
			osDelay(1);
			timeout_ms--;
		}
		last_frame_counter = capsense_frame_counter;

		for (uint8_t logical = 0; logical < 34; logical++) {
			uint8_t channel = capsense_channel_for_logical(logical);
			uint16_t raw = Touch.channel_raw[channel];
			uint16_t baseline = capsense_baseline[channel];
			uint16_t positive_delta = raw > baseline ? (uint16_t) (raw - baseline) : 0;
			uint16_t step_delta = 0;

			if (prev_raw_valid) {
				step_delta = raw > prev_raw[logical] ? (uint16_t) (raw - prev_raw[logical]) :
						(uint16_t) (prev_raw[logical] - raw);
			}

			if (capsense_touch_status[logical] || (raw >= 0xFF00) ||
					(positive_delta > CAPSENSE_AUTO_THRESHOLD_BASELINE_DELTA_MAX) ||
					(prev_raw_valid && (step_delta > CAPSENSE_AUTO_THRESHOLD_STEP_MAX))) {
				frame_valid = 0;
				break;
			}

			raw_frame[logical] = raw;
			positive_delta_frame[logical] = positive_delta;
		}

		if (!frame_valid) {
			stable_frames = 0;
			sample_count = 0;
			prev_raw_valid = 0;
			continue;
		}

		memcpy(prev_raw, raw_frame, sizeof(prev_raw));
		prev_raw_valid = 1;

		if (stable_frames < CAPSENSE_AUTO_THRESHOLD_STABLE_FRAMES) {
			stable_frames++;
			continue;
		}

		for (uint8_t logical = 0; logical < 34; logical++) {
			uint16_t raw = raw_frame[logical];
			uint16_t positive_delta = positive_delta_frame[logical];

			capsense_auto_threshold_samples[logical][sample_count] = raw;

			if (sample_count == 0) {
				min_raw[logical] = raw;
				max_raw[logical] = raw;
				max_positive_delta[logical] = positive_delta;
				continue;
			}

			if (raw < min_raw[logical]) {
				min_raw[logical] = raw;
			}
			if (raw > max_raw[logical]) {
				max_raw[logical] = raw;
			}
			if (positive_delta > max_positive_delta[logical]) {
				max_positive_delta[logical] = positive_delta;
			}
		}

		sample_count++;
	}

	for (uint8_t logical = 0; logical < 34; logical++) {
		uint32_t peak_to_peak = (uint32_t) max_raw[logical] - (uint32_t) min_raw[logical];
		uint32_t trimmed_span;
		uint32_t threshold;
		uint32_t positive_candidate = (uint32_t) max_positive_delta[logical] * CAPSENSE_AUTO_THRESHOLD_POS_MULTIPLIER;

		memcpy(capsense_auto_threshold_sorted, capsense_auto_threshold_samples[logical],
				sizeof(capsense_auto_threshold_sorted));

		for (uint16_t i = 1; i < CAPSENSE_AUTO_THRESHOLD_SAMPLE_COUNT; i++) {
			uint16_t value = capsense_auto_threshold_sorted[i];
			uint16_t j = i;

			while ((j > 0) && (capsense_auto_threshold_sorted[j - 1] > value)) {
				capsense_auto_threshold_sorted[j] = capsense_auto_threshold_sorted[j - 1];
				j--;
			}
			capsense_auto_threshold_sorted[j] = value;
		}

		trimmed_span = (uint32_t) capsense_auto_threshold_sorted[trim_high_index] -
				(uint32_t) capsense_auto_threshold_sorted[trim_low_index];
		threshold = trimmed_span * CAPSENSE_AUTO_THRESHOLD_TRIM_MULTIPLIER;
		if ((peak_to_peak * CAPSENSE_AUTO_THRESHOLD_P2P_CAP_MULTIPLIER) < threshold) {
			threshold = peak_to_peak * CAPSENSE_AUTO_THRESHOLD_P2P_CAP_MULTIPLIER;
		}
		if (positive_candidate > threshold) {
			threshold = positive_candidate;
		}
		if (threshold < CAPSENSE_AUTO_THRESHOLD_MIN) {
			threshold = CAPSENSE_AUTO_THRESHOLD_MIN;
		}
		if (threshold > CAPSENSE_AUTO_THRESHOLD_MAX) {
			threshold = CAPSENSE_AUTO_THRESHOLD_MAX;
		}

		Flash.touch_threshold[logical] = (uint16_t) threshold;
		thresholds_out[logical] = (uint16_t) threshold;

		if ((uint16_t) threshold < threshold_min) {
			threshold_min = (uint16_t) threshold;
		}
		if ((uint16_t) threshold > threshold_max) {
			threshold_max = (uint16_t) threshold;
		}
	}

	flash_write(Flash.raw_flash);

	if (min_threshold_out != NULL) {
		*min_threshold_out = threshold_min;
	}
	if (max_threshold_out != NULL) {
		*max_threshold_out = threshold_max;
	}

	return 1;
}
