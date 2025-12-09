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

#define CAPSENSE_DURATION_A 100
#define CAPSENSE_DURATION_B 1000

#define PSOC_DEBUG

uint8_t uart_dma_buffer[128];

extern UART_HandleTypeDef huart4;
extern DMA_HandleTypeDef hdma_uart4_rx;
extern FlashData Flash;

packet_capsense_t Touch;
uint16_t capsense_raw_windows[10][34];
uint8_t capsense_raw_bet = 0;
uint16_t capsense_duration[8] = {0};
uint16_t capsense_level[8] = {0};
uint16_t capsense_orinigal[34];
uint16_t capsense_baseline[34];
uint16_t capsense_threshold[34];
uint8_t capsense_data_ready = 0;
uint8_t capsense_bit;
uint8_t capsense_touch_status[34];

#ifdef PSOC_DEBUG
typedef union{
    float raw_data_fl[2];
    uint8_t raw_data_u8[8];
}vofa;

vofa vofa1;
#endif

//void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
//{
////	if(Size != 70){
////		return;
////	}
//	//CDC_Transmit_FS((uint8_t*)uart_dma_buffer, Size);
//    if (huart->Instance == UART4)
//    {
//        HAL_UART_DMAStop(&huart4);
//        if((uart_dma_buffer[0] == 0) && (uart_dma_buffer[1] == 0)){
//        	memcpy(&Touch.data[0],uart_dma_buffer,70);
//        }
//        HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart_dma_buffer, 70);
//        __HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
//    }
//}
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
void Touch_UART_Handler(){
	if(__HAL_UART_GET_FLAG(&huart4, UART_FLAG_IDLE)){
		__HAL_UART_CLEAR_IDLEFLAG(&huart4);
		HAL_UART_DMAStop(&huart4);
        if(uart_dma_buffer[0] == 0){
            checksum = 0;
            for(uint8_t i = 0;i<69;i++){
                checksum += uart_dma_buffer[i];
            }
            if(checksum == uart_dma_buffer[69])
            {
            	memcpy(&Touch.data[0],uart_dma_buffer+1,68);
            	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 1);
            }
        }
		HAL_UART_Receive_DMA(&huart4,uart_dma_buffer,70);
	}
}

void capsense_init(){
	__HAL_UART_ENABLE_IT(&huart4, UART_IT_IDLE);
	HAL_UART_Receive_DMA(&huart4, uart_dma_buffer, 70);
	__HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
	osDelay(100);
	for(uint8_t i = 0;i<34;i++){
		capsense_baseline[i] = Touch.channel_raw[i];
		capsense_orinigal[i] = Touch.channel_raw[i];
	}
	capsense_data_ready = 0;
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
	//BLOCK A
	for(uint8_t i = 0;i<8;i++){
		if(capsense_baseline[i] == 0){
			capsense_baseline[i] = Touch.channel_raw[i];
			capsense_orinigal[i] = Touch.channel_raw[i];
		}
//		if(capsense_duration[i] == 0){
//			capsense_level[i] = Touch.channel_raw[Flash.touch_sheet[i]];
//		}else if(capsense_duration[i] > CAPSENSE_DURATION_B){
//			if(Touch.channel_raw[Flash.touch_sheet[i]] > capsense_level[i]){
//				//Touch.channel_raw[Flash.touch_sheet[i]] = capsense_level[i];
//			}
//		}else{
//			float level = (capsense_level[Flash.touch_sheet[i]] * 0.9) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.1);
//			capsense_level[i] = level;
//		}
		if(capsense_duration[i] > CAPSENSE_DURATION_A){
			capsense_baseline[i] = capsense_orinigal[i];
		}
		int variance = Touch.channel_raw[Flash.touch_sheet[i]] - capsense_baseline[Flash.touch_sheet[i]];
		if((variance > Flash.touch_threshold[i]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
			capsense_touch_status[i] = 1;
			if(capsense_baseline[Flash.touch_sheet[i]] + CAPSENSE_BASELINE_VARIANCE < Touch.channel_raw[Flash.touch_sheet[i]]){
				float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.99) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.01);
				capsense_baseline[Flash.touch_sheet[i]] = baseline > 60000 ? 60000 : baseline;
			}
			if(capsense_duration[i] < 10000){
				capsense_duration[i] ++;
			}
		}
		else{
			capsense_touch_status[i] = 0;
			capsense_duration[i] = 0;
			if(capsense_baseline[Flash.touch_sheet[i]] + CAPSENSE_BASELINE_VARIANCE_A > Touch.channel_raw[Flash.touch_sheet[i]]){
				float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.9) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.1);
				capsense_baseline[Flash.touch_sheet[i]] = baseline > 60000 ? 60000 : baseline;
			}
		}
	}
	//BLOCK B
	for(uint8_t i = 8;i<16;i++){
		if(capsense_baseline[i] == 0){
			capsense_baseline[i] = Touch.channel_raw[i];
		}
		if(capsense_baseline[Flash.touch_sheet[i]] + CAPSENSE_BASELINE_VARIANCE_B > Touch.channel_raw[Flash.touch_sheet[i]]){
			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.8) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.2);
			capsense_baseline[Flash.touch_sheet[i]] = baseline;
		}
		int variance = Touch.channel_raw[Flash.touch_sheet[i]] - capsense_baseline[Flash.touch_sheet[i]];
		if((variance > Flash.touch_threshold[i]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
			capsense_touch_status[i] = 1;
		}
		else{
			capsense_touch_status[i] = 0;
		}
	}
	//BLOCK C
	for(uint8_t i = 16;i<18;i++){
		if(capsense_baseline[i] == 0){
			capsense_baseline[i] = Touch.channel_raw[i];
		}
		if(capsense_baseline[Flash.touch_sheet[i]] + CAPSENSE_BASELINE_VARIANCE_C > Touch.channel_raw[Flash.touch_sheet[i]]){
			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.8) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.2);
			capsense_baseline[Flash.touch_sheet[i]] = baseline;
		}
		int variance = Touch.channel_raw[Flash.touch_sheet[i]] - capsense_baseline[Flash.touch_sheet[i]];
		if((variance > Flash.touch_threshold[i]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
			capsense_touch_status[i] = 1;
		}
		else{
			capsense_touch_status[i] = 0;
		}
	}
	//BLOCK D
	for(uint8_t i = 18;i<26;i++){
		if(capsense_baseline[i] == 0){
			capsense_baseline[i] = Touch.channel_raw[i];
		}
		if(capsense_baseline[Flash.touch_sheet[i]] + CAPSENSE_BASELINE_VARIANCE_D > Touch.channel_raw[Flash.touch_sheet[i]]){
			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.8) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.2);
			capsense_baseline[Flash.touch_sheet[i]] = baseline;
		}
		int variance = Touch.channel_raw[Flash.touch_sheet[i]] - capsense_baseline[Flash.touch_sheet[i]];
		if((variance > Flash.touch_threshold[i]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
			capsense_touch_status[i] = 1;
		}
		else{
			capsense_touch_status[i] = 0;
		}
	}
	//BLOCK E
	for(uint8_t i = 26;i<34;i++){
		if(capsense_baseline[i] == 0){
			capsense_baseline[i] = Touch.channel_raw[i];
		}
		if(capsense_baseline[Flash.touch_sheet[i]] + CAPSENSE_BASELINE_VARIANCE_E > Touch.channel_raw[Flash.touch_sheet[i]]){
			float baseline = (capsense_baseline[Flash.touch_sheet[i]] * 0.8) + (Touch.channel_raw[Flash.touch_sheet[i]] * 0.2);
			capsense_baseline[Flash.touch_sheet[i]] = baseline;
		}
		int variance = Touch.channel_raw[Flash.touch_sheet[i]] - capsense_baseline[Flash.touch_sheet[i]];
		if((variance > Flash.touch_threshold[i]) || (Touch.channel_raw[Flash.touch_sheet[i]] >= 0xFF00)){
			capsense_touch_status[i] = 1;
		}
		else{
			capsense_touch_status[i] = 0;
		}
	}
	capsense_data_ready = 0;

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



#ifdef PSOC_DEBUG
	uint8_t debug_channel = 0;
	vofa1.raw_data_fl[0] = Touch.channel_raw[debug_channel];
//	vofa1.raw_data_fl[1] = capsense_baseline[debug_channel];
	vofa1.raw_data_fl[1] = capsense_baseline[debug_channel] + Flash.touch_threshold[debug_channel];
	static uint8_t tmp[12] = {0,0,0,0,0,0,0,0,0,0,0x80,0x7f};
	memcpy(tmp,vofa1.raw_data_u8,8);
	CDC_Transmit(0, tmp,12);
#endif
}
