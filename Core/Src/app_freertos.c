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
#include "queue.h"
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
#include "usbd_cdc_acm_if.h"
#include "usbd_hid_custom_if.h"
#include "usbd_hid_keyboard.h"
#include "capsense.h"
#include "capsense_sim.h"
#include "flash.h"
#include "stack.h"
#include "dfu_jump.h"
#include "app_version.h"
#include "firmware_header.h"
#include "input_snapshot.h"
#include "serial_commands.h"
#include "serial_reports.h"
#include "usbd_desc.h"
#include "usb_reporter.h"
#include "benchmark.h"
#include "debug_mode.h"
#include "app_state.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TOUCH_REPORT_PERIOD_MS 1u
#define CAPSENSE_LED_ONLINE_HOLD_MS 300u
#define CAPSENSE_LED_ERROR_HOLD_MS 500u
#define CAPSENSE_LED_WAIT_BLINK_HALF_PERIOD_MS 500u
#define CAPSENSE_LED_ERROR_BLINK_HALF_PERIOD_MS 125u
const char VERSION[] = FIRMWARE_VERSION;

// Firmware Header instance placed in specific section
__attribute__((section(".fw_header"))) __attribute__((used))
const FirmwareHeader_t fw_header = {
    .magic = FW_HEADER_MAGIC,
    .version_major = FW_VER_MAJOR,
    .version_minor = FW_VER_MINOR,
    .version_patch = FW_VER_PATCH,
    .git_hash = FW_GIT_HASH,
    .build_timestamp = FW_BUILD_TIME,
    .crc32 = 0,
    .reserved = {0} 
};

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern USBD_HandleTypeDef hUsbDevice;
uint8_t touch_cmd_flag = 0;
volatile uint8_t touch_scan_flag = 0;
extern uint8_t keyboard_sheet[14];
uint8_t player = 1;
uint8_t current_touch_status[34];
uint8_t current_button_status[2];
extern volatile uint8_t capsense_data_ready;
volatile uint8_t debug_flag = 0;
volatile uint8_t debug_stream_mode = SERIAL_DEBUG_STREAM_MODE_FOCUS;
volatile uint8_t debug_exit_reset_pending = 0u;
volatile uint32_t debug_exit_reset_deadline_ms = 0u;
/* USER CODE END Variables */
osThreadId TouchTaskHandle;
osThreadId ButtonTaskHandle;
osThreadId CommandTaskHandle;
osThreadId LEDTaskHandle;
osThreadId UsbTxTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void capsense_update_link_led(void);
/* USER CODE END FunctionPrototypes */

void Touch_Task(void const * argument);
void Button_Task(void const * argument);
void Command_Task(void const * argument);
void LED_Task(void const * argument);
void UsbTx_Task(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

static void capsense_update_link_led(void)
{
	uint32_t now = HAL_GetTick();
	uint32_t last_good_tick = 0;
	uint32_t last_error_tick = 0;
	uint8_t protocol_version = 0;
	GPIO_PinState led_state = GPIO_PIN_RESET;

	capsense_link_state_get(&last_good_tick, &last_error_tick, &protocol_version);

	if ((last_error_tick != 0u) &&
			((uint32_t)(now - last_error_tick) <= CAPSENSE_LED_ERROR_HOLD_MS)) {
		if (((now / CAPSENSE_LED_ERROR_BLINK_HALF_PERIOD_MS) & 0x01u) != 0u) {
			led_state = GPIO_PIN_SET;
		}
	} else if (protocol_version == 0u) {
		if (((now / CAPSENSE_LED_WAIT_BLINK_HALF_PERIOD_MS) & 0x01u) != 0u) {
			led_state = GPIO_PIN_SET;
		}
	} else if ((last_good_tick != 0u) &&
			((uint32_t)(now - last_good_tick) <= CAPSENSE_LED_ONLINE_HOLD_MS)) {
		led_state = GPIO_PIN_SET;
	}

	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, led_state);
}

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  serial_command_init();
  input_snapshot_reset();
  UsbTxGuard_Init();
  if (usb_reporter_init() == 0u) {
	  Error_Handler();
  }
  usb_reporter_reset();

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
  osThreadDef(TouchTask, Touch_Task, osPriorityNormal, 0, 256);
  TouchTaskHandle = osThreadCreate(osThread(TouchTask), NULL);

  /* definition and creation of ButtonTask */
  osThreadDef(ButtonTask, Button_Task, osPriorityIdle, 0, 128);
  ButtonTaskHandle = osThreadCreate(osThread(ButtonTask), NULL);

  /* definition and creation of CommandTask */
  /* Command_Task executes a large command switch plus several reply builders.
   * The pre-debug stack budget was conservative but much safer in practice.
   */
  osThreadDef(CommandTask, Command_Task, osPriorityBelowNormal, 0, 384);
  CommandTaskHandle = osThreadCreate(osThread(CommandTask), NULL);

  /* definition and creation of LEDTask */
  osThreadDef(LEDTask, LED_Task, osPriorityIdle, 0, 256);
  LEDTaskHandle = osThreadCreate(osThread(LEDTask), NULL);

  /* definition and creation of UsbTxTask */
  osThreadDef(UsbTxTask, UsbTx_Task, osPriorityAboveNormal, 0, 256);
  UsbTxTaskHandle = osThreadCreate(osThread(UsbTxTask), NULL);
  if ((TouchTaskHandle == NULL) || (ButtonTaskHandle == NULL) || (CommandTaskHandle == NULL) ||
		  (LEDTaskHandle == NULL) || (UsbTxTaskHandle == NULL)) {
	  Error_Handler();
  }

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
	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 0);
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, 1);
	flash_read(Flash.raw_flash);
	(void) flash_config_sanitize();
	player = Flash.controller_role;
	osDelay(1000);
	capsense_init();
	benchmark_counter_init();
	heart_beat_refresh();
	while(1)
	{
		osDelay(TOUCH_REPORT_PERIOD_MS);
		capsense_service_pending_reset();
		benchmark_emit_pending_event();
		if(!capsense_data_ready){
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, 1);
		}
		if(capsense_take_latest_snapshot()){
			capsense_check();
			stack_flow_touch(current_touch_status);
			capsense_input_snapshot_publish();
		}
		capsense_debug_service();
		capsense_update_link_led();
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
	memset(current_button_status, 0, sizeof(current_button_status));
	input_snapshot_publish_buttons(current_button_status, HAL_GetTick());
	while(1){
		osDelay(3);
		button_scan();
		capsense_sim_maybe_generate_buttons(button, HAL_GetTick());
		stack_flow_button(current_button_status);
		input_snapshot_publish_buttons(current_button_status, HAL_GetTick());
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
  serial_frame_t rx_frame;
  benchmark_counter_init();
	for(;;)
  {
	(void) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
	(void) serial_command_drain_rx_stream();
	while (serial_command_pop(&rx_frame)) {
		serial_command_set_response_transport(
				(serial_command_transport_t)rx_frame.transport);
		if ((rx_frame.len != 0u) && (rx_frame.data[0] == 0xFFu)) {
			if (serial_protocol_frame_valid(rx_frame.data, rx_frame.len) == 0u) {
				serial_command_set_response_transport(
						SERIAL_COMMAND_TRANSPORT_CDC);
				continue;
			}
			serial_commands_process_frame(&rx_frame, benchmark_cycles64());
		} else {
			serial_commands_process_legacy_ascii(rx_frame.data, rx_frame.len);
		}
		serial_command_set_response_transport(SERIAL_COMMAND_TRANSPORT_CDC);
	}
	if ((debug_exit_reset_pending != 0u) &&
			((int32_t)(HAL_GetTick() - debug_exit_reset_deadline_ms) >= 0)) {
		debug_exit_reset_now();
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
	LED_UART_Init();
	FET_LED_Init();
//	HAL_TIM_Base_Start_IT(&htim7);
	LED_StateMachineInit(HAL_GetTick());
	while(1){
		LED_Task_ProcessPending();
		LED_ServiceStateMachine(HAL_GetTick());
		LED_ServiceFade();
		LED_ServiceRefresh();
		osDelay(1);
	}
  /* USER CODE END LED_Task */
}

void UsbTx_Task(void const * argument)
{
  /* USER CODE BEGIN UsbTx_Task */
	(void) argument;

	for(;;){
		usb_reporter_service(debug_flag, debug_stream_mode,
				heart_beat_active(), touch_scan_flag,
				benchmark_quiet_active());
		osDelay(1);
	}
  /* USER CODE END UsbTx_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

