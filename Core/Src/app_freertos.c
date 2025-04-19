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
#include "usbd_cdc_if.h"
#include "capsense.h"
#include "flash.h"
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
uint8_t touch_cmd_flag = 0;
uint8_t heart_beat = 0xff;
extern FlashData Flash;
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
  osThreadDef(ButtonTask, Button_Task, osPriorityNormal, 0, 128);
  ButtonTaskHandle = osThreadCreate(osThread(ButtonTask), NULL);

  /* definition and creation of CommandTask */
  osThreadDef(CommandTask, Command_Task, osPriorityNormal, 0, 128);
  CommandTaskHandle = osThreadCreate(osThread(CommandTask), NULL);

  /* definition and creation of LEDTask */
  osThreadDef(LEDTask, LED_Task, osPriorityNormal, 0, 128);
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
  /* init code for USB_Device */
  MX_USB_Device_Init();
  /* USER CODE BEGIN Touch_Task */
	/* Infinite loop */
	//mai_touch
	flash_read(Flash.raw_flash);
	if(Flash.system_config != VERSION){
		for(uint8_t i = 0;i<34;i++){
			Flash.touch_threshold[i] = 2000;
		}
		uint8_t touch_sheet[34] = {
			32,28,23,19,15,11,6,2,
			33,29,24,20,16,12,7,3,
			27,8,
			0,30,25,21,17,13,9,4,
			1,31,26,22,18,14,10,5,
		};
		memcpy(Flash.touch_sheet,touch_sheet,34);
		Flash.system_config = VERSION;
		flash_write(Flash.raw_flash);
	}
	osDelay(1000);
	capsense_init();
	while(1)
	{
		osDelay(2);
		capsense_check();
		uint8_t cmd_mai2io[14] = {0xff,1,10,0,0,0,0,0,0,0,0,0,0,10};
		uint8_t cmd_touch[9] = {};
		for(uint8_t j = 0;j<7;j++){
			for(uint8_t i = 0;i<5;i++){
				if(j == 6 && i == 4){
					break;
					//没有35个触摸点
				}
				if(capsense_touch_status[Flash.touch_sheet[i+j*5]]){
					cmd_mai2io[j+6] |= (1 << i);
				}
			}
		}
		cmd_mai2io[3] = button & 0b00001111;
		cmd_mai2io[4] = button & 0b11110000;
		cmd_mai2io[5] = extend_button;
//		if(touch_cmd_flag == 1){
		if(heart_beat != 0){
			CDC_Transmit_FS((uint8_t*)cmd_mai2io, 14);
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
	osDelay(1000);
	button_init();
	while(1){
		osDelay(2);
		button_scan();
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
				memcpy(RGB_data_raw,rxBuffer+3,24);
				__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,rxBuffer[27]);
				__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,rxBuffer[28]);
				__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,rxBuffer[29]);
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
				CDC_Transmit_FS((uint8_t*)cmd_tmp, 7);
				break;
			}

			case SERIAL_CMD_WRITE_MONO_THRESHOLD:{
				memcpy(&Flash.touch_threshold[rxBuffer[3]],&rxBuffer[4],2);
				flash_write(Flash.raw_flash);
				uint8_t cmd_tmp[5] = {0xff,6,1,1,7};
				CDC_Transmit_FS((uint8_t*)cmd_tmp, 5);
				break;
			}
			case SERIAL_CMD_READ_TOUCH_SHEET:{
				uint8_t cmd_tmp[38] = {0xff,7,34};
				memcpy(cmd_tmp + 3,Flash.touch_sheet,34);
				for(uint8_t i = 0;i<37;i++){
					cmd_tmp[37] += cmd_tmp[i];
				}
				CDC_Transmit_FS((uint8_t*)cmd_tmp, 38);
				break;

			}
			case SERIAL_CMD_WRITE_TOUCH_SHEET:{
				memcpy(Flash.touch_sheet,&rxBuffer[3],34);
				flash_write(Flash.raw_flash);
				uint8_t cmd_tmp[5] = {0xff,8,1,1,9};
				CDC_Transmit_FS((uint8_t*)cmd_tmp, 5);
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
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,0);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,0);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,0);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_2);
	while(1){
		for(uint8_t i = 0;i<8;i++){
			LED_set(i*2,RGB_data_raw[i*3],RGB_data_raw[i*3+1],RGB_data_raw[i*3+2]);
			LED_set(i*2+1,RGB_data_raw[i*3],RGB_data_raw[i*3+1],RGB_data_raw[i*3+2]);
		}
		LED_refresh();
		osDelay(10);
	}
  /* USER CODE END LED_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

