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
#include "usbd_cdc_if.h"
#include "cmsis_os.h"
#include "flash.h"
#define CAPSENSE_BASELINE_VARIANCE 255 //基线计算所能允许的最大方差，如果超过此数值则不更新基线
#define CAPSENSE_LOW_BASELINE_RESET 500 //低基线复位，如果基线减去raw大于这个数值，则立即以当前的raw作为基线。

uint8_t uart_dma_buffer[128];
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart4;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern DMA_HandleTypeDef hdma_uart4_rx;
extern FlashData Flash;

packet_capsense_t Touch;
uint16_t capsense_raw_windows[10][34];
uint8_t capsense_raw_bet = 0;
uint16_t capsense_baseline[34];
uint16_t capsense_threshold[34];
uint8_t capsense_data_ready = 0;
uint8_t capsense_bit;
uint8_t capsense_touch_status[34];

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
//	if(Size != 70){
//		return;
//	}
	//CDC_Transmit_FS((uint8_t*)uart_dma_buffer, Size);
    if (huart->Instance == UART4)
    {
        HAL_UART_DMAStop(&huart4);
        if((uart_dma_buffer[0] == 0) && (uart_dma_buffer[1] == 0)){
        	memcpy(&Touch.data[0],uart_dma_buffer,70);
        }
        HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart_dma_buffer, 70);
        __HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
    }
    capsense_data_ready = 1;
}

void capsense_init(){
	__HAL_UART_ENABLE_IT(&huart4, UART_IT_IDLE);
	HAL_UART_Receive_DMA(&huart4, uart_dma_buffer, 70);
	__HAL_DMA_DISABLE_IT(&hdma_uart4_rx, DMA_IT_HT);
	osDelay(100);
	for(uint8_t i = 0;i<34;i++){
		capsense_baseline[i] = Touch.channel_raw[i];
	}
	capsense_data_ready = 0;
}

void capsense_baseline_updata(uint8_t channel){
	float average = 0;
	for(uint8_t k = 0;k < 10;k++){
		average += capsense_raw_windows[k][channel]/10;
	}
	float variance = 0;
	for (uint8_t k = 0;k < 10;k++) {
		variance += powf(capsense_raw_windows[k][channel] - average, 2);
	}
	variance /= 10;
	if(variance < CAPSENSE_BASELINE_VARIANCE){
		capsense_baseline[channel] = average;
	}
}

void capsense_check(){
	//while(!capsense_data_ready);
//	memcpy(&capsense_raw_windows[capsense_raw_bet][0],&Touch.channel_raw[0],34*2);
//	capsense_raw_bet ++;
//	if(capsense_raw_bet > 9){
//		capsense_raw_bet = 0;
//	}
//	for(uint8_t i = 0;i<34;i++){
//		if((capsense_baseline[i] > Touch.channel_raw[i]) && (capsense_baseline[i] - Touch.channel_raw[i] > CAPSENSE_LOW_BASELINE_RESET)){
//			capsense_baseline[i] = Touch.channel_raw[i];
//		}else if(capsense_raw_bet == 0){
//			capsense_baseline_updata(i);
//		}
//	}
	for(uint8_t i = 0;i<34;i++){
		if(capsense_baseline[i] + Flash.touch_threshold[i] < Touch.channel_raw[i]){
			capsense_touch_status[i] = 1;
		}else{
			capsense_touch_status[i] = 0;
		}
	}
	capsense_data_ready = 0;

}
