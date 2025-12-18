#include "LED.h"
#include "stdio.h"
#include "usart.h"
#include "stdbool.h"
#include <string.h>

#define NUM_LED 16
#define PRE_BUTTON_LED 2
#define WS2812_HIGH 143
#define WS2812_LOW 67

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;
uint8_t led_write_buffer[64];
uint8_t dummyEEPRom[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
PacketReq req;
PacketRes res;

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

//uint8_t mai_led_default_response[8] = {0xe0,0x01,0x11,0x03,0x01,0x31,0x01,0x48};
//uint8_t mai_led_eeprom_response[9] = {0xe0,0x01 ,0x11,0x04,0x01,0x7c,0x01,0x00,0x94};
//uint8_t mai_led_boardinfo_response[18] = {0xe0,0x01,0x11,0x0d,0x01,0xf0,0x01,0x31,0x35,0x30,0x37,0x30,0x2d,0x30,0x34,0xff,0x90,0x2e};
//uint8_t mai_led_boardstatus_response[12] = {0xe0,0x01,0x11,0x07,0x01,0xf1,0x01,00,00,00,00,0x0c};
//uint8_t mai_led_protocolversion_response[11] = {0xe0,0x01,0x11,0x06,0x01,0xf3,0x01,0x01,0x00,0x00,0x0e};

uint8_t WS2812_data_raw[24] = {
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};
uint8_t WS2812_data_button[24];
uint8_t WS2812_data_billboard[24];
uint8_t WS2812_data[NUM_LED * 3]; //16LED
uint16_t WS2812_data_DMA_buffer[64 + NUM_LED * 24 + 64];
uint8_t led_uart_buffer_rx[64];
uint8_t led_uart_buffer_tx[64];
uint8_t led_uart_tmp[64];

typedef struct {
    uint8_t start[3];
    uint8_t current[3];
    uint8_t target[3];
    uint16_t duration;
    uint16_t elapsed;
} FadeContext;

FadeContext fade_ctx[NUM_LED];

volatile uint32_t timer7_count = 0;
volatile uint32_t timer7_target = 0;
volatile uint8_t timer7_active = 0;

void LED_set(uint8_t led_no,uint8_t r,uint8_t g,uint8_t b){
	if(led_no > 8){
		return;
	}
	for(uint8_t i = 0;i<PRE_BUTTON_LED;i++){
		WS2812_data[(led_no * 2  + i ) * 3  ] = r;
		WS2812_data[(led_no * 2  + i ) * 3 + 1] = g;
		WS2812_data[(led_no * 2  + i ) * 3 + 2] = b;
	}
}

void LED_refresh()
{
	for(uint8_t i = 0 ;i<NUM_LED;i++)
	{
		for(uint8_t j = 0 ;j <8;j++)
		{
			WS2812_data_DMA_buffer[(i*3)*8+j+ 64] = (gamma8[WS2812_data[i*3+1]] & (1<<(7-j))) ? WS2812_HIGH:WS2812_LOW;
		}
		for(uint8_t j = 0 ;j <8;j++)
		{
			WS2812_data_DMA_buffer[(i*3+1)*8+j+ 64] = (gamma8[WS2812_data[i*3]] & (1<<(7-j))) ? WS2812_HIGH:WS2812_LOW;
		}
		for(uint8_t j = 0 ;j <8;j++)
		{
			WS2812_data_DMA_buffer[(i*3+2)*8+j+ 64] = (gamma8[WS2812_data[i*3+2]] & (1<<(7-j))) ? WS2812_HIGH:WS2812_LOW;
		}
	}
	HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_2, (uint32_t *)WS2812_data_DMA_buffer, (64 + NUM_LED * 24 + 64));
}

void LED_update_button(){
	for(uint8_t i = 0;i<8;i++){
		LED_set(i,WS2812_data_button[i*3],WS2812_data_button[i*3+1],WS2812_data_button[i*3+2]);
	}
	LED_refresh();
}

void FET_LED_Init(){
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_4);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,5);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,5);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,5);
}
void FET_LED_Update(uint8_t BodyLED,uint8_t SideLED,uint8_t CamRingLED,uint8_t CamRecLED,uint8_t ReaderLED){
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,BodyLED);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,CamRingLED);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,SideLED);
}
void LED_UART_Init(){
	//memset(WS2812_data_DMA_buffer,0,64);
	for(uint16_t i = 0;i < 128 + NUM_LED * 24 + 64;i++){
		WS2812_data_DMA_buffer[i] = 0;
	}
//	memset(WS2812_data_DMA_buffer,0xff,64);
//	memset(WS2812_data_DMA_buffer + NUM_LED * 24 + 64, 0xff ,64);
//	memset(WS2812_data_DMA_buffer + NUM_LED * 24 + 128, 0 ,64);
	__HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
	HAL_UART_Receive_DMA(&huart1, led_uart_buffer_rx, 64);
	__HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
}

void LED_UART_IRQHandler(){
	if(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)){
		HAL_UART_DMAStop(&huart1);
		__HAL_UART_CLEAR_IDLEFLAG(&huart1);
		//CDC_Transmit(0,(uint8_t*)led_uart_buffer_rx, 39);
		memcpy(led_uart_tmp,led_uart_buffer_rx,64);
		HAL_UART_Receive_DMA(&huart1,led_uart_buffer_rx,64);
		LED_Task_Process();
	}
}

void set_led_immediate(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= NUM_LED) return;
    fade_ctx[index].start[0] = r;
    fade_ctx[index].start[1] = g;
    fade_ctx[index].start[2] = b;
    fade_ctx[index].current[0] = r;
    fade_ctx[index].current[1] = g;
    fade_ctx[index].current[2] = b;
    fade_ctx[index].target[0] = r;
    fade_ctx[index].target[1] = g;
    fade_ctx[index].target[2] = b;
    fade_ctx[index].duration = 0;
    LED_set(index, r, g, b);
}

void set_led_fade(uint8_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
    if (index >= NUM_LED) return;
    if (speed == 0) {
        set_led_immediate(index, r, g, b);
        return;
    }
    // Start from current color
    memcpy(fade_ctx[index].start, fade_ctx[index].current, 3);
    fade_ctx[index].target[0] = r;
    fade_ctx[index].target[1] = g;
    fade_ctx[index].target[2] = b;
    fade_ctx[index].duration = (4095 / speed * 8);
    fade_ctx[index].elapsed = 0;
}

void LED_Fade_IRQHandler(){
    for(int i=0; i<NUM_LED; i++) {
        if(fade_ctx[i].duration > 0) {
            fade_ctx[i].elapsed++;
            if(fade_ctx[i].elapsed >= fade_ctx[i].duration) {
                fade_ctx[i].elapsed = fade_ctx[i].duration;
                memcpy(fade_ctx[i].current, fade_ctx[i].target, 3);
                fade_ctx[i].duration = 0;
            } else {
                // Integer Lerp
                for(int c=0; c<3; c++) {
                    int32_t start = fade_ctx[i].start[c];
                    int32_t target = fade_ctx[i].target[c];
                    fade_ctx[i].current[c] = start + (target - start) * fade_ctx[i].elapsed / fade_ctx[i].duration;
                }
            }
            LED_set(i, fade_ctx[i].current[0], fade_ctx[i].current[1], fade_ctx[i].current[2]);
        }
    }
    LED_refresh();
}

uint8_t led_packet_check(uint8_t* data,uint8_t len) {
	bool escape = false;
	uint8_t raw_pos = 0;
	uint8_t req_pos = 0;
	uint8_t checksum = 0;
	while(raw_pos<len){
		if(data[raw_pos] == Sync){
			req.length = 0x04;
			raw_pos++;
			break;
		}
		raw_pos++;
	}
	if(raw_pos == len){
		return 0;
	}
	while(req_pos != req.length + 3){
		if (data[raw_pos] == 0xD0) {
			escape = true;
			raw_pos++;
		}else if (escape) {
			req.bytes[req_pos] = data[raw_pos] + 1;
			checksum += req.bytes[req_pos];
			escape = false;
			raw_pos++;
			req_pos++;
		}else{
			req.bytes[req_pos] = data[raw_pos];
			checksum += req.bytes[req_pos];
			raw_pos++;
			req_pos++;
		}
		if(raw_pos == len){
			return 0;
		}
	}
	req.bytes[req_pos] = data[raw_pos];
	if(checksum == req.bytes[req.length + 3]){
		return req.cmd;
	}else{
		return 0;
	}
}

void led_packet_write() {
  uint8_t checksum = 0, len = 0;
  if (res.cmd == 0) {
    return;
  }
  memset(led_write_buffer,0,16);
  led_write_buffer[0] = 0xE0;
  uint8_t current_pos = 0;
  while (len <= res.length + 3) {
    uint8_t w;
    if (len == res.length + 3) {
      w = checksum;
    } else {
      w = res.bytes[len];
      checksum += w;
    }
    if (w == 0xE0 || w == 0xD0) {
    	led_write_buffer[++current_pos] = 0xD0;
    	led_write_buffer[++current_pos] = --w;
    } else {
    	led_write_buffer[++current_pos] = w;
    }
    len++;
  }
  res.cmd = 0;
  HAL_UART_Transmit_DMA(&huart1, led_write_buffer, ++current_pos);
  memset(res.bytes,0,12);
}

void res_init(uint8_t length, uint8_t status, uint8_t report) {
  res.dstNodeID = req.srcNodeID;
  res.srcNodeID = req.dstNodeID;
  res.length = 3 + length;
  res.status = status;
  res.cmd = req.cmd;
  res.report = report;
}

void LED_Task_Process(){
	switch(led_packet_check(led_uart_tmp,64)){
//		case 0:
//			return;
		case SetLedGs8Bit:
			set_led_immediate(req.index, req.color[0], req.color[1], req.color[2]);
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case SetLedGs8BitMulti:
			for(uint8_t i = 0; i < req.end; i++){
                set_led_immediate(req.start + i, req.Multi_color[0], req.Multi_color[1], req.Multi_color[2]);
			}
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case SetLedGs8BitMultiFade:
			for(uint8_t i = 0; i < req.end; i++){
                set_led_fade(req.start + i, req.Multi_color[0], req.Multi_color[1], req.Multi_color[2], req.speed);
			}
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case SetLedFet:
			FET_LED_Update(req.BodyLED, req.SideLED, req.CamRingLED, req.CamRecLED, req.ReaderLED);
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case SetLedGsUpdate:
			LED_refresh();
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case SetEEPRom:
			dummyEEPRom[req.Set_adress] = req.writeData;
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case GetEEPRom:
			//GetEEPRom
			res.eepData = dummyEEPRom[req.Get_adress];
			res_init(1,AckStatus_Ok,AckReport_Ok);
			//HAL_UART_Transmit_DMA(&huart1, mai_led_eeprom_response, 9);
			break;
		case GetBoardInfo:
			  memcpy(res.boardNo, "15070-04\xFF\x90\x00\x30", 12);
			  res.firmRevision = 144;
			  res_init(10,AckStatus_Ok,AckReport_Ok);
//			  res.dstNodeID = 0x01;
//			  res.srcNodeID = 0x11;
			break;
		case GetBoardStatus:
			res.timeoutStat = 0;
			res.timeoutSec = 1;
			res.pwmIo = 0;
			res.fetTimeout = 0;
			res_init(4,AckStatus_Ok,AckReport_Ok);
			break;
		case GetFirmSum:
			res.sum_upper = 0;
			res.sum_lower = 0;
			res_init(2,AckStatus_Ok,AckReport_Ok);
			break;
		case GetProtocolVersion:
			res.appliMode = 1;         // IsNeedFirmUpdate = false
			res.major = 1;
			res.minor = 1;
			res_init(3,AckStatus_Ok,AckReport_Ok);
			break;
		default:
			res_init(0,AckStatus_Ok,AckReport_Ok);
	}
	led_packet_write();
	memset(led_uart_tmp,0,64);
}
