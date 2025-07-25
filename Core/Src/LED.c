#include "LED.h"
#include "stdio.h"
#include "usart.h"

#define NUM_LED 16
#define WS2812_HIGH 140
#define WS2812_LOW 70

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

const uint8_t gamma8[256] = {
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,
  2,   2,   2,   2,   2,   3,   3,   3,   4,   4,   4,   5,   5,   5,   6,   6,
  6,   7,   7,   7,   8,   8,   9,   9,   9,  10,  10,  11,  11,  12,  12,  13,
 13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  19,  19,  20,  20,  21,  22,
 22,  23,  23,  24,  25,  25,  26,  27,  27,  28,  29,  29,  30,  31,  32,  32,
 33,  34,  35,  35,  36,  37,  38,  38,  39,  40,  41,  42,  42,  43,  44,  45,
 46,  46,  47,  48,  49,  50,  51,  52,  53,  53,  54,  55,  56,  57,  58,  59,
 60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,
 76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  87,  88,  89,  90,  91,  92,
 93,  94,  96,  97,  98,  99, 100, 101, 103, 104, 105, 106, 107, 109, 110, 111,
112, 114, 115, 116, 117, 119, 120, 121, 122, 124, 125, 126, 128, 129, 130, 131,
133, 134, 135, 137, 138, 139, 141, 142, 144, 145, 146, 148, 149, 150, 152, 153,
155, 156, 158, 159, 160, 162, 163, 165, 166, 168, 169, 171, 172, 174, 175, 177,
178, 180, 181, 183, 184, 186, 187, 189, 190, 192, 193, 195, 196, 198, 200, 201,
203, 204, 206, 208, 209, 211, 212, 214, 216, 217, 219, 221, 222, 224, 225, 227,
229, 231, 232, 234, 236, 237, 239, 241, 242, 244, 246, 248, 249, 251, 253, 255
};

uint8_t mai_led_default_response[8] = {0xe0,0x01,0x11,0x03,0x01,0x31,0x01,0x48};
uint8_t mai_led_eeprom_response[9] = {0xe0,0x01 ,0x11,0x04,0x01,0x7c,0x01,0x00,0x94};
uint8_t mai_led_boardinfo_response[18] = {0xe0,0x01,0x11,0x0d,0x01,0xf0,0x01,0x31,0x35,0x30,0x37,0x30,0x2d,0x30,0x34,0xff,0x90,0x2e};
uint8_t mai_led_boardstatus_response[12] = {0xe0,0x01,0x11,0x07,0x01,0xf1,0x01,00,00,00,00,0x0c};
uint8_t mai_led_protocolversion_response[11] = {0xe0,0x01,0x11,0x06,0x01,0xf3,0x01,0x01,0x00,0x00,0x0e};

uint8_t WS2812_data_raw[24] = {
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};
static uint8_t WS2812_data[48] = {0xff}; //16LED
static uint32_t WS2812_data_DMA_buffer[64 + NUM_LED * 24 + 64] = {WS2812_HIGH + WS2812_LOW};
uint8_t led_uart_buffer_rx[39];
uint8_t led_uart_buffer_tx[12];
uint8_t led_uart_tmp[39];

uint8_t led_fade_flag;
uint8_t led_fade_target[2];
uint8_t led_fade_buff[3];
uint16_t led_fade_time;

void LED_set(uint8_t led_no,uint8_t r,uint8_t g,uint8_t b){
	if(led_no >= NUM_LED){
		return;
	}
	WS2812_data[led_no * 3] = r;
	WS2812_data[led_no * 3 + 1] = g;
	WS2812_data[led_no * 3 + 2] = b;
}

void LED_refresh()
{
	for(uint8_t i = 0 ;i<16;i++)
	{
		for(uint8_t j = 0 ;j <8;j++)
		{
			WS2812_data_DMA_buffer[(i*3)*8+j+64] = (gamma8[WS2812_data[i*3+1]] & (1<<j)) ? WS2812_HIGH:WS2812_LOW;
		}
		for(uint8_t j = 0 ;j <8;j++)
		{
			WS2812_data_DMA_buffer[(i*3+1)*8+j+64] = (gamma8[WS2812_data[i*3]] & (1<<j)) ? WS2812_HIGH:WS2812_LOW;
		}
		for(uint8_t j = 0 ;j <8;j++)
		{
			WS2812_data_DMA_buffer[(i*3+2)*8+j+64] = (gamma8[WS2812_data[i*3+2]] & (1<<j)) ? WS2812_HIGH:WS2812_LOW;
		}
	}
	HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_2, (uint32_t *)WS2812_data_DMA_buffer, 64 + NUM_LED * 24 + 64);
}
void FET_LED_Init(){
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,0);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,0);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,0);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_4);
}
void FET_LED_Update(uint8_t BodyLed,uint8_t ExtLed,uint8_t SideLed){
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,BodyLed);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,ExtLed);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,SideLed);
}
void LED_UART_Init(){
	__HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
	HAL_UART_Receive_DMA(&huart1, led_uart_buffer_rx, 39);
	__HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
}

void LED_UART_IRQHandler(){
	if(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)){
		HAL_UART_DMAStop(&huart1);
		__HAL_UART_CLEAR_IDLEFLAG(&huart1);
		//CDC_Transmit_FS((uint8_t*)uart_dma_buffer, 70);
		memcpy(led_uart_tmp,led_uart_buffer_rx,39);
		HAL_UART_Receive_DMA(&huart1,led_uart_buffer_rx,39);
	}
}

void LED_Fade_IRQHandler(){
	led_fade_flag = 0;
	for(uint8_t i = led_fade_target[0];i < led_fade_target[1];i++){
		LED_set(2*i,0,0,0);
		LED_set(2*i+1,0,0,0);
	}
	LED_refresh();
}

void LED_Task_Process(){
	if(led_uart_tmp[0] == 0xE0){
		switch(led_uart_tmp[4]){
			case 0x31:
				//setLedGs8Bit
				LED_set(2*led_uart_tmp[5],led_uart_tmp[6],led_uart_tmp[7],led_uart_tmp[8]);
				LED_set(2*led_uart_tmp[5]+1,led_uart_tmp[6],led_uart_tmp[7],led_uart_tmp[8]);
				mai_led_default_response[5] = 0x31;
				mai_led_default_response[6] = 0x48;
				HAL_UART_Transmit_DMA(&huart1, mai_led_default_response, 8);
				break;
			case 0x32:
				//setLedGs8BitMulti
				for(uint8_t i = led_uart_tmp[5];i < led_uart_tmp[6];i++){
					LED_set(2*i,led_uart_tmp[8],led_uart_tmp[9],led_uart_tmp[10]);
					LED_set(2*i+1,led_uart_tmp[8],led_uart_tmp[9],led_uart_tmp[10]);
				}
				mai_led_default_response[5] = 0x32;
				mai_led_default_response[6] = 0x49;
				HAL_UART_Transmit_DMA(&huart1, mai_led_default_response, 8);
				break;
			case 0x33:
				//setLedGs8BitMultiFade
				led_fade_flag = 2;
				memcpy(led_fade_target,led_uart_tmp + 5,2);
				memcpy(led_fade_buff,led_uart_tmp + 8,3);
				led_fade_time = 4095/led_uart_tmp[9]*8;
				__HAL_TIM_SetAutoreload(&htim7,led_fade_time);
				mai_led_default_response[5] = 0x33;
				mai_led_default_response[6] = 0x4a;
				HAL_UART_Transmit_DMA(&huart1, mai_led_default_response, 8);
				break;
			case 0x39:
				//setLedFet
				//BodyLed ExtLed SideLed
				FET_LED_Update(led_uart_tmp[5],led_uart_tmp[6],led_uart_tmp[7]);
				mai_led_default_response[5] = 0x39;
				mai_led_default_response[6] = 0x50;
				HAL_UART_Transmit_DMA(&huart1, mai_led_default_response, 8);
				break;
			case 0x3c:
				//SetLedGsUpdate
				if(led_fade_flag == 2){
					HAL_TIM_Base_Start_IT(&htim7);
					led_fade_flag = 1;
				}
				LED_refresh();
				mai_led_default_response[5] = 0x3c;
				mai_led_default_response[6] = 0x53;
				HAL_UART_Transmit_DMA(&huart1, mai_led_default_response, 8);
				break;
			case 0x7c:
				//GetEEPRom
				HAL_UART_Transmit_DMA(&huart1, mai_led_eeprom_response, 9);
				break;
			case 0xf0:
				//getBoardInfo
				HAL_UART_Transmit_DMA(&huart1, mai_led_boardinfo_response, 18);
				break;
			case 0xf1:
				//getBoardStatus
				HAL_UART_Transmit_DMA(&huart1, mai_led_boardstatus_response, 12);
				break;
			case 0xf3:
				//getProtocolVersion
				HAL_UART_Transmit_DMA(&huart1, mai_led_protocolversion_response, 11);
				break;
		}
		memset(led_uart_tmp,0,39);
	}
}
