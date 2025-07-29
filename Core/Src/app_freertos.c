/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usb_device.h"
#include "usart.h"
#include "button.h"
#include "dma.h"
#include "tim.h"
#include "LED.h"
#include "slider.h"
#include "usbd_cdc_acm_if.h"
#include "usbd_hid_keyboard.h"
#include "capsense.h"
#include "flash.h"
#include "stack.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define VERSION 1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern USBD_HandleTypeDef hUsbDevice;
uint8_t touch_cmd_flag = 0;
uint8_t touch_scan_flag = 0;
uint8_t heart_beat = 0xff;
extern FlashData Flash;
uint8_t keyboard_sheet[14] = {
	0x1A,0x08,0x07,0x06,0x1B,0x1D,0x04,0x14,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F
};
uint8_t player = 1;
uint8_t current_touch_status[34];
uint8_t current_button_status[2];
/* USER CODE END Variables */
osThreadId TouchTaskHandle;
osThreadId ButtonTaskHandle;
osThreadId CommandTaskHandle;
osThreadId LEDTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void Touch_Task(void const * argument);
void Button_Task(void const * argument);
void Command_Task(void const * argument);
void LED_Task(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of TouchTask */
  osThreadDef(TouchTask, Touch_Task, osPriorityNormal, 0, 128);
  TouchTaskHandle = osThreadCreate(osThread(TouchTask), NULL);

  /* definition and creation of ButtonTask */
  osThreadDef(ButtonTask, Button_Task, osPriorityIdle, 0, 128);
  ButtonTaskHandle = osThreadCreate(osThread(ButtonTask), NULL);

  /* definition and creation of CommandTask */
  osThreadDef(CommandTask, Command_Task, osPriorityIdle, 0, 128);
  CommandTaskHandle = osThreadCreate(osThread(CommandTask), NULL);

  /* definition and creation of LEDTask */
  osThreadDef(LEDTask, LED_Task, osPriorityIdle, 0, 128);
  LEDTaskHandle = osThreadCreate(osThread(LEDTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_Touch_Task */
/**
  * @brief  Function implementing the TouchTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_Touch_Task */
void Touch_Task(void const * argument)
{
  /* USER CODE BEGIN Touch_Task */
	/* Infinite loop */
	//mai_touch
	LED_set(0,255,255,255);
	LED_refresh();
	flash_read(Flash.raw_flash);
	if(Flash.system_config != VERSION){
		for(uint8_t i = 0;i<34;i++){
			Flash.touch_threshold[i] = 2000;
		}
//		uint8_t touch_sheet[34] = {
//			32,28,23,19,15,11,6,2,
//			33,29,24,20,16,12,7,3,
//			27,8,
//			0,30,25,21,17,13,9,4,
//			1,31,26,22,18,14,10,5,
//		};
		uint8_t touch_sheet_default[34] ={
				0,1,2,3,4,5,6,7,8,
				9,10,11,12,13,14,15,
				16,17,
				18,19,20,21,22,23,24,
				25,26,27,28,29,30,31,32,33

				//wuyu
//				32,28,23,19,15,11,6,2,
//				33,29,24,20,16,12,7,3,
//				27,10,
//				0,30,25,21,17,13,8,4,
//				2,31,26,22,18,14,9,5

//				jing
//				31,27,22,18,14,10,5,1,
//				32,28,23,19,15,11,6,2,
//				25,8,
//				33,29,24,20,16,12,7,3,
//				0,30,26,21,17,13,9,4

		};
		memcpy(Flash.touch_sheet,touch_sheet_default,34);
		Flash.delay_setting[0] = 0;
		Flash.delay_setting[1] = 0;
		Flash.system_config = VERSION;
		flash_write(Flash.raw_flash);
	}
	if(Flash.delay_setting[0] > 9){
		Flash.delay_setting[0] = 9;
		flash_write(Flash.raw_flash);
	}
	if(Flash.delay_setting[1] > 9){
		Flash.delay_setting[1] = 9;
		flash_write(Flash.raw_flash);
	}
	osDelay(1000);
	capsense_init();
	//HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 1);
	while(1)
	{
		osDelay(1);
		capsense_check();
		uint8_t cmd_mai2io[14] = {0xff,1,10,0,0,0,0,0,0,0,0,0,0,10};
		uint8_t cmd_mai2touch[9] = {0x28,0,0,0,0,0,0,0,0x29};
		stack_flow_touch(current_touch_status);
		stack_flow_button(current_button_status);
		for(uint8_t j = 0;j<7;j++){
			for(uint8_t i = 0;i<5;i++){
				if(j == 6 && i == 4){
					break;
					//没有35个触摸点
				}
				if(current_touch_status[Flash.touch_sheet[i+j*5]]){
					cmd_mai2io[j+6] |= (1 << i);
				}
			}
		}
		cmd_mai2io[3] = current_button_status[0] & 0b00001111;
		cmd_mai2io[4] = current_button_status[0] & 0b11110000;
		cmd_mai2io[5] = current_button_status[1];
		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 0);
//		if(touch_cmd_flag == 1){
		if(heart_beat != 0){
			CDC_Transmit(0,(uint8_t*)cmd_mai2io, 14);
		}else if(touch_scan_flag != 0){
			memcpy(cmd_mai2touch+1,cmd_mai2io+6,7);
			CDC_Transmit(0,(uint8_t*)cmd_mai2touch, 9);
		}
			//CDC_Transmit((uint8_t*)vofa1.data, 8);
			//touch_cmd_flag = 0;
//		}
//		else if(touch_scan_flag == 1){
//			CDC_Transmit(0,(uint8_t*)cmd_touch, 9);
//		}

	}
  /* USER CODE END Touch_Task */
}

/* USER CODE BEGIN Header_Button_Task */
/**
* @brief Function implementing the ButtonTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Button_Task */
void Button_Task(void const * argument)
{
  /* USER CODE BEGIN Button_Task */
  /* Infinite loop */
	//mai_key
	uint8_t keyboard_buffer[14];
	memset(keyboard_buffer,0,12);
	osDelay(1000);
	button_init();
	while(1){
		osDelay(1);
		button_scan();
		if(heart_beat == 0){
			stack_flow_button(current_button_status);
			for(uint8_t i = 0;i<8;i++){
				keyboard_buffer[i] =  (current_button_status[0] & (1 << i)) ? keyboard_sheet[i] : 0;
			}
			for(uint8_t i = 0;i<6;i++){
				keyboard_buffer[i+8] =  (current_button_status[1] & (1 << i)) ? keyboard_sheet[i+8] : 0;
			}
			USBD_HID_Keybaord_SendReport(&hUsbDevice, keyboard_buffer, 14);
		}
	}
  /* USER CODE END Button_Task */
}

/* USER CODE BEGIN Header_Command_Task */
/**
* @brief Function implementing the CommandTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Command_Task */
void Command_Task(void const * argument)
{
  /* USER CODE BEGIN Command_Task */
  /* Infinite loop */
  for(;;)
  {
    osDelay(5);
	if ((rxLen != 0)&&(rxBuffer[0] == 0xff)){
		switch(rxBuffer[1]){
			case SERIAL_CMD_LED:
				if(rxBuffer[2] != 27){
					break;
				}
				memcpy(WS2812_data_raw,rxBuffer+3,24);
				FET_LED_Update(rxBuffer[27],rxBuffer[28],rxBuffer[29]);
				break;
			case SERIAL_CMD_SCAN_START:
				break;
			case SERIAL_CMD_SCAN_STOP:
				break;
			case SERIAL_CMD_READ_MONO_THRESHOLD:{
				uint8_t cmd_tmp[7] = {0xff,5,3,0,0,0,0};
				cmd_tmp[3] = rxBuffer[3];
				memcpy(cmd_tmp + 4,&Flash.touch_threshold[cmd_tmp[3]],2);
				for(uint8_t i = 0;i<6;i++){
					cmd_tmp[6] += cmd_tmp[i];
				}
				CDC_Transmit(0,(uint8_t*)cmd_tmp, 7);
				break;
			}

			case SERIAL_CMD_WRITE_MONO_THRESHOLD:{
				memcpy(&Flash.touch_threshold[rxBuffer[3]],&rxBuffer[4],2);
				flash_write(Flash.raw_flash);
				uint8_t cmd_tmp[5] = {0xff,6,1,1,7};
				CDC_Transmit(0,(uint8_t*)cmd_tmp, 5);
				break;
			}
			case SERIAL_CMD_READ_TOUCH_SHEET:{
				uint8_t cmd_tmp[38] = {0xff,7,34};
				memcpy(cmd_tmp + 3,Flash.touch_sheet,34);
				for(uint8_t i = 0;i<37;i++){
					cmd_tmp[37] += cmd_tmp[i];
				}
				CDC_Transmit(0,(uint8_t*)cmd_tmp, 38);
				break;

			}
			case SERIAL_CMD_WRITE_TOUCH_SHEET:{
				//memcpy(Flash.touch_sheet,&rxBuffer[3],34);
				for(uint8_t i = 0;i<34;i++){
					Flash.touch_sheet[i] = rxBuffer[i+3];
				}
				flash_write(Flash.raw_flash);
				uint8_t cmd_tmp[5] = {0xff,8,1,1,9};
				CDC_Transmit(0,(uint8_t*)cmd_tmp, 5);
				break;
			}
			case SERIAL_CMD_READ_DELAY_SETTING:{
				if(rxBuffer[2] != 1){
					break;
				}else{
					uint8_t cmd_tmp[6] = {0xff,0x12,2};
					cmd_tmp[3] = rxBuffer[3];
					cmd_tmp[4] = Flash.delay_setting[rxBuffer[3]];
					for(uint8_t i = 0;i<5;i++){
						cmd_tmp[5] += cmd_tmp[i];
					}
					CDC_Transmit(0,(uint8_t*)cmd_tmp, 6);
					break;
				}
			}
			case SERIAL_CMD_WRITE_DELAY_SETTING:{
				if(rxBuffer[2] != 2){
					break;
				}else{
					Flash.delay_setting[rxBuffer[3]] = rxBuffer[4];
					uint8_t cmd_tmp[5] = {0xff,0x13,1};
					flash_write(Flash.raw_flash);
					cmd_tmp[3] = rxBuffer[3];
					for(uint8_t i = 0;i<4;i++){
						cmd_tmp[4] += cmd_tmp[i];
					}
					CDC_Transmit(0,(uint8_t*)cmd_tmp, 5);
					break;
				}
				break;
			}
			case SERIAL_CMD_RESET:
				break;
			case SERIAL_CMD_HEART_BEAT:
				heart_beat = 0xff;
				break;
			case SERIAL_CMD_GET_BOARD_INFO:
				break;
		}
	}else if((rxLen != 0)&&(rxBuffer[0] == 0x7B)){
		char cmd_tmp[6] = "(RSET)";
		switch (rxBuffer[1]){
			case 0x53:{
				//{STAT}
				touch_scan_flag = 1;
				break;
			}
			case 0x48:
				//{HALT}
				touch_scan_flag = 0;
				break;
			case 0x52:
				//{Rxxx}
				touch_scan_flag = 0;
				if(rxBuffer[3] == 0x45){
					//{RSET}
					break;
				}else if(rxBuffer[3] == 0x72){
					player = 2;
					//Set Touch Panel Ratio
					//TODO
					memcpy(cmd_tmp+1,rxBuffer+1,4);
				}else if(rxBuffer[3] == 0x6b){
					player = 2;
					//Set Touch Panel Sensitivity
					memcpy(cmd_tmp+1,rxBuffer+1,4);
				}
				CDC_Transmit(0,(uint8_t*)cmd_tmp,6);
				break;

			case 0x4c:
				//{Lxxx}
				touch_scan_flag = 0;
				player = 1;
				if(rxBuffer[3] == 0x72){
					//Set Touch Panel Ratio
					//TODO
					memcpy(cmd_tmp+1,rxBuffer+1,4);
				}else if(rxBuffer[3] == 0x6b){
					//Set Touch Panel Sensitivity
					memcpy(cmd_tmp+1,rxBuffer+1,4);
				}
				CDC_Transmit(0,(uint8_t*)cmd_tmp,6);
				break;
			default:
				break;
		}
	}
	rxLen = 0;
	if(heart_beat != 0){
		heart_beat --;
	}
  }
  /* USER CODE END Command_Task */
}

/* USER CODE BEGIN Header_LED_Task */
/**
* @brief Function implementing the LEDTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_LED_Task */
void LED_Task(void const * argument)
{
  /* USER CODE BEGIN LED_Task */
  /* Infinite loop */
	FET_LED_Init();
	while(1){
		for(uint8_t i = 0;i<8;i++){
			LED_set(i*2,0xff,0xff,0xff);
			LED_set(i*2+1,0xff,0xff,0xff);
		}
		LED_refresh();
		if(heart_beat != 0){
			for(uint8_t i = 0;i<8;i++){
				LED_set(i*2,0xff,0xff,0xff);
				LED_set(i*2+1,0xff,0xff,0xff);
			}
			LED_refresh();
		}else{
			LED_Task_Process();
			if(led_fade_flag == 1){
				uint16_t current_fade_time = TIM7->CNT;
				float process = current_fade_time / led_fade_time;
				for(uint8_t i = led_fade_target[0];i < led_fade_target[1];i++){
					LED_set(2*i,led_fade_buff[0] * process ,led_fade_buff[1] * process ,led_fade_buff[2] * process);
					LED_set(2*i+1,led_fade_buff[0] * process ,led_fade_buff[1] * process ,led_fade_buff[2] * process);
				}
				LED_refresh();
			}
		}
		osDelay(10);
	}
  /* USER CODE END LED_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

