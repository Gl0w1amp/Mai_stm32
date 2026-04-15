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
#include "slider.h"
#include "usbd_cdc_acm_if.h"
#include "usbd_hid_custom_if.h"
#include "usbd_hid_keyboard.h"
#include "capsense.h"
#include "flash.h"
#include "stack.h"
#include "dfu_jump.h"
#include "app_version.h"
#include "firmware_header.h"
#include "usbd_desc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct usb_tx_packet {
	uint16_t len;
	uint32_t enqueue_tick;
	uint8_t data[64];
} usb_tx_packet_t;

typedef struct usb_cdc_tx_stats {
	uint32_t high_enqueued_count;
	uint32_t low_enqueued_count;
	uint32_t high_drop_count;
	uint32_t low_drop_count;
	uint32_t tx_ok_count;
	uint32_t tx_busy_retry_count;
	uint32_t tx_fail_retry_count;
	uint32_t tx_giveup_count;
	uint32_t last_tx_latency_ms;
	uint32_t max_tx_latency_ms;
	uint32_t last_retry_count;
	uint32_t max_retry_count;
} usb_cdc_tx_stats_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CONFIG_VERSION 1
#define BENCHMARK_REPLY_OVERHEAD 24
#define BENCHMARK_MAX_PAYLOAD (64 - BENCHMARK_REPLY_OVERHEAD)
#define BENCHMARK_EVENT_PAYLOAD 8
#define BENCHMARK_EVENT_REPLY_PAYLOAD 24
#define BENCHMARK_EVENT_DELAY_MS_DEFAULT 10
#define BENCHMARK_QUIET_PERIOD_MS 30
#define TOUCH_REPORT_PERIOD_MS 1u
#define USB_TX_HIGH_QUEUE_LENGTH 8
#define USB_TX_LOW_QUEUE_LENGTH 8
#define BENCHMARK_TX_RETRY_COUNT 50
#define CAPSENSE_LED_ONLINE_HOLD_MS 300u
#define CAPSENSE_LED_ERROR_HOLD_MS 500u
#define CAPSENSE_LED_WAIT_BLINK_HALF_PERIOD_MS 500u
#define CAPSENSE_LED_ERROR_BLINK_HALF_PERIOD_MS 125u
#define HEART_BEAT_HOLD_MS 300u
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

/* DFU jump function */
void Jump_To_DFU_Bootloader(void)
{
    uint32_t i;
    void (*SysMemBootJump)(void);
    
    /* Disable all interrupts */
    __disable_irq();
    
    /* Set the clock to the default state */
    HAL_RCC_DeInit();
    
    /* Clear Interrupt Enable Register & Interrupt Pending Register */
    for (i = 0; i < 5; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    
    /* Enable the SYSCFG peripheral clock*/
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    
    /* Remap system memory to address 0x0000 0000 in address space */
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
    
    /* Set jump memory location for system memory */
    /* Use address with 4 bytes offset as reset location is 0x0000 0004 */
    SysMemBootJump = (void (*)(void)) (*((uint32_t *)(0x1FFF0000 + 4)));
    
    /* Set the main stack pointer to the system memory value */
    __set_MSP(*(uint32_t *)0x1FFF0000);
    
    /* Call the function to jump to system memory */
    SysMemBootJump();
    
    /* Jump is done successfully */
    while (1)
    {
        /* Code should not reach this loop */
    }
}

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern USBD_HandleTypeDef hUsbDevice;
uint8_t touch_cmd_flag = 0;
volatile uint8_t touch_scan_flag = 0;
static volatile uint32_t heart_beat_deadline_ms = 0;
extern uint8_t keyboard_sheet[14];
extern uint8_t debug_channel;
uint8_t player = 1;
uint8_t current_touch_status[34];
uint8_t current_button_status[2];
extern volatile uint8_t capsense_data_ready;
volatile uint8_t debug_flag = 0;
volatile uint8_t debug_stream_mode = SERIAL_DEBUG_STREAM_MODE_FOCUS;
volatile uint32_t benchmark_quiet_until_ms = 0;
volatile uint8_t benchmark_event_pending = 0;
volatile uint32_t benchmark_event_due_ms = 0;
volatile uint32_t benchmark_event_sequence = 0;
volatile uint8_t benchmark_event_transport = 0;
static uint32_t benchmark_last_cycles = 0;
static uint32_t benchmark_cycles_high = 0;
static usb_cdc_tx_stats_t usb_cdc_tx_stats = {0};
/* USER CODE END Variables */
osThreadId TouchTaskHandle;
osThreadId ButtonTaskHandle;
osThreadId CommandTaskHandle;
osThreadId LEDTaskHandle;
osThreadId UsbTxTaskHandle;
QueueHandle_t usb_tx_high_queue = NULL;
QueueHandle_t usb_tx_low_queue = NULL;
QueueSetHandle_t usb_tx_queue_set = NULL;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void benchmark_counter_init(void);
static uint32_t benchmark_cycles(void);
static uint64_t benchmark_cycles64(void);
static void benchmark_write_u32_le(uint8_t *dst, uint32_t value);
static void benchmark_write_u64_le(uint8_t *dst, uint64_t value);
static uint32_t benchmark_read_u32_le(const uint8_t *src);
static uint8_t benchmark_quiet_active(void);
static void capsense_update_link_led(void);
static uint8_t usb_tx_enqueue(QueueHandle_t queue, const uint8_t *buf, uint16_t len);
static uint8_t usb_tx_enqueue_high(const uint8_t *buf, uint16_t len);
static uint8_t usb_tx_enqueue_low(const uint8_t *buf, uint16_t len);
static void usb_cdc_tx_stats_reset(void);
static void usb_cdc_tx_stats_snapshot(usb_cdc_tx_stats_t *stats_out, uint32_t *high_depth_out, uint32_t *low_depth_out);
static void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, uint64_t dispatch_cycles);
static void serial_send_benchmark_event(uint32_t sequence, uint64_t event_cycles);
static void hid_send_benchmark_event(uint32_t sequence, uint64_t event_cycles);
static void benchmark_emit_pending_event(void);
static uint8_t serial_binary_frame_valid(const uint8_t *frame, uint8_t len);
static void heart_beat_refresh(void);
static uint8_t heart_beat_active(void);
static uint8_t controller_role_normalize(uint8_t role);
/* USER CODE END FunctionPrototypes */

void Touch_Task(void const * argument);
void Button_Task(void const * argument);
void Command_Task(void const * argument);
void LED_Task(void const * argument);
void UsbTx_Task(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

static void benchmark_counter_init(void)
{
	static uint8_t initialized = 0;

	if (initialized) {
		return;
	}

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	DWT->CYCCNT = 0;
	initialized = 1;
}

static uint32_t benchmark_cycles(void)
{
	return DWT->CYCCNT;
}

static uint64_t benchmark_cycles64(void)
{
	uint32_t now = benchmark_cycles();
	uint64_t full;

	taskENTER_CRITICAL();
	if (now < benchmark_last_cycles) {
		benchmark_cycles_high++;
	}
	benchmark_last_cycles = now;
	full = (((uint64_t) benchmark_cycles_high) << 32) | now;
	taskEXIT_CRITICAL();

	return full;
}

static void benchmark_write_u32_le(uint8_t *dst, uint32_t value)
{
	dst[0] = (uint8_t)(value & 0xFF);
	dst[1] = (uint8_t)((value >> 8) & 0xFF);
	dst[2] = (uint8_t)((value >> 16) & 0xFF);
	dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static void benchmark_write_u64_le(uint8_t *dst, uint64_t value)
{
	for (uint8_t i = 0; i < 8; i++) {
		dst[i] = (uint8_t)((value >> (i * 8)) & 0xFF);
	}
}

static uint32_t benchmark_read_u32_le(const uint8_t *src)
{
	return ((uint32_t) src[0]) |
		(((uint32_t) src[1]) << 8) |
		(((uint32_t) src[2]) << 16) |
		(((uint32_t) src[3]) << 24);
}

static uint8_t benchmark_quiet_active(void)
{
	return ((int32_t)(benchmark_quiet_until_ms - HAL_GetTick()) > 0) ? 1 : 0;
}

static void heart_beat_refresh(void)
{
	heart_beat_deadline_ms = HAL_GetTick() + HEART_BEAT_HOLD_MS;
}

static uint8_t heart_beat_active(void)
{
	return ((int32_t)(heart_beat_deadline_ms - HAL_GetTick()) > 0) ? 1u : 0u;
}

static uint8_t controller_role_normalize(uint8_t role)
{
	return (role == 2u) ? 2u : 1u;
}

static void usb_cdc_tx_stats_reset(void)
{
	memset(&usb_cdc_tx_stats, 0, sizeof(usb_cdc_tx_stats));
}

static void usb_cdc_tx_stats_snapshot(usb_cdc_tx_stats_t *stats_out, uint32_t *high_depth_out, uint32_t *low_depth_out)
{
	if (stats_out != NULL) {
		*stats_out = usb_cdc_tx_stats;
	}
	if (high_depth_out != NULL) {
		*high_depth_out = (usb_tx_high_queue != NULL) ? (uint32_t) uxQueueMessagesWaiting(usb_tx_high_queue) : 0u;
	}
	if (low_depth_out != NULL) {
		*low_depth_out = (usb_tx_low_queue != NULL) ? (uint32_t) uxQueueMessagesWaiting(usb_tx_low_queue) : 0u;
	}
}

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

static uint8_t usb_tx_enqueue(QueueHandle_t queue, const uint8_t *buf, uint16_t len)
{
	usb_tx_packet_t packet;
	BaseType_t result;

	if (queue == NULL || buf == NULL || len == 0 || len > sizeof(packet.data)) {
		return 0;
	}

	packet.len = len;
	packet.enqueue_tick = HAL_GetTick();
	memcpy(packet.data, buf, len);

	result = xQueueSend(queue, &packet, 0);
	if (queue == usb_tx_high_queue) {
		if (result == pdPASS) {
			usb_cdc_tx_stats.high_enqueued_count++;
		} else {
			usb_cdc_tx_stats.high_drop_count++;
		}
	} else if (queue == usb_tx_low_queue) {
		if (result == pdPASS) {
			usb_cdc_tx_stats.low_enqueued_count++;
		} else {
			usb_cdc_tx_stats.low_drop_count++;
		}
	}

	return result == pdPASS ? 1 : 0;
}

static uint8_t usb_tx_enqueue_high(const uint8_t *buf, uint16_t len)
{
	return usb_tx_enqueue(usb_tx_high_queue, buf, len);
}

static uint8_t usb_tx_enqueue_low(const uint8_t *buf, uint16_t len)
{
	return usb_tx_enqueue(usb_tx_low_queue, buf, len);
}

uint8_t serial_cdc_tx_enqueue_high(const uint8_t *buf, uint16_t len)
{
	return usb_tx_enqueue_high(buf, len);
}

uint8_t serial_cdc_tx_enqueue_low(const uint8_t *buf, uint16_t len)
{
	return usb_tx_enqueue_low(buf, len);
}

uint32_t serial_cdc_tx_low_spaces_available(void)
{
	return (usb_tx_low_queue != NULL) ?
			(uint32_t) uxQueueSpacesAvailable(usb_tx_low_queue) : 0u;
}

/* Echoes the benchmark payload and attaches device-side cycle timestamps. */
static void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, uint64_t dispatch_cycles)
{
	uint8_t cmd_tmp[1 + 1 + 1 + BENCHMARK_MAX_PAYLOAD + 8 + 8 + 4 + 1];
	uint8_t idx = 0;
	uint64_t tx_cycles = benchmark_cycles64();

	if (payload_len > BENCHMARK_MAX_PAYLOAD) {
		payload_len = BENCHMARK_MAX_PAYLOAD;
	}

	cmd_tmp[idx++] = 0xFF;
	cmd_tmp[idx++] = cmd;
	cmd_tmp[idx++] = payload_len + 20;
	memcpy(&cmd_tmp[idx], payload, payload_len);
	idx += payload_len;
	benchmark_write_u64_le(&cmd_tmp[idx], dispatch_cycles);
	idx += 8;
	benchmark_write_u64_le(&cmd_tmp[idx], tx_cycles);
	idx += 8;
	benchmark_write_u32_le(&cmd_tmp[idx], SystemCoreClock);
	idx += 4;
	cmd_tmp[idx] = 0;
	for (uint8_t i = 0; i < idx; i++) {
		cmd_tmp[idx] += cmd_tmp[i];
	}
	(void) usb_tx_enqueue_high(cmd_tmp, idx + 1);
}

static void serial_send_benchmark_event(uint32_t sequence, uint64_t event_cycles)
{
	uint8_t cmd_tmp[1 + 1 + 1 + BENCHMARK_EVENT_REPLY_PAYLOAD + 1];
	uint8_t idx = 0;
	uint64_t tx_cycles = benchmark_cycles64();

	cmd_tmp[idx++] = 0xFF;
	cmd_tmp[idx++] = SERIAL_CMD_BENCHMARK_EVENT;
	cmd_tmp[idx++] = BENCHMARK_EVENT_REPLY_PAYLOAD;
	benchmark_write_u32_le(&cmd_tmp[idx], sequence);
	idx += 4;
	benchmark_write_u64_le(&cmd_tmp[idx], event_cycles);
	idx += 8;
	benchmark_write_u64_le(&cmd_tmp[idx], tx_cycles);
	idx += 8;
	benchmark_write_u32_le(&cmd_tmp[idx], SystemCoreClock);
	idx += 4;
	cmd_tmp[idx] = 0;
	for (uint8_t i = 0; i < idx; i++) {
		cmd_tmp[idx] += cmd_tmp[i];
	}
	(void) usb_tx_enqueue_high(cmd_tmp, idx + 1);
}

static void hid_send_benchmark_event(uint32_t sequence, uint64_t event_cycles)
{
	uint64_t tx_cycles = benchmark_cycles64();
	(void) mai2_hid_benchmark_send_report((uint16_t) sequence, event_cycles, tx_cycles, SystemCoreClock);
}

static void benchmark_emit_pending_event(void)
{
	uint32_t sequence = 0;
	uint64_t event_cycles = 0;
	uint32_t now_ms = HAL_GetTick();

	if (!benchmark_event_pending || ((int32_t)(now_ms - benchmark_event_due_ms) < 0)) {
		return;
	}

	taskENTER_CRITICAL();
	if (!benchmark_event_pending || ((int32_t)(HAL_GetTick() - benchmark_event_due_ms) < 0)) {
		taskEXIT_CRITICAL();
		return;
	}
	benchmark_event_pending = 0;
	sequence = benchmark_event_sequence;
	event_cycles = benchmark_cycles64();
	uint8_t transport = benchmark_event_transport;
	taskEXIT_CRITICAL();

	if (transport == 2) {
		hid_send_benchmark_event(sequence, event_cycles);
	} else {
		serial_send_benchmark_event(sequence, event_cycles);
	}
}

static uint8_t serial_binary_frame_valid(const uint8_t *frame, uint8_t len)
{
	uint8_t checksum = 0;
	uint8_t expected_len;

	if ((frame == NULL) || (len < 4u) || (frame[0] != 0xFFu)) {
		return 0;
	}

	expected_len = (uint8_t) (frame[2] + 4u);
	if (expected_len != len) {
		return 0;
	}

	for (uint8_t i = 0; i < (uint8_t) (len - 1u); i++) {
		checksum += frame[i];
	}

	return checksum == frame[len - 1u];
}

void slider_notify_command_ready_from_isr(void)
{
	BaseType_t higher_priority_task_woken = pdFALSE;

	if (CommandTaskHandle == NULL) {
		return;
	}

	vTaskNotifyGiveFromISR((TaskHandle_t) CommandTaskHandle, &higher_priority_task_woken);
	portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  serial_command_init();
  usb_tx_high_queue = xQueueCreate(USB_TX_HIGH_QUEUE_LENGTH, sizeof(usb_tx_packet_t));
  usb_tx_low_queue = xQueueCreate(USB_TX_LOW_QUEUE_LENGTH, sizeof(usb_tx_packet_t));
  usb_tx_queue_set = xQueueCreateSet(USB_TX_HIGH_QUEUE_LENGTH + USB_TX_LOW_QUEUE_LENGTH);
  if (usb_tx_queue_set != NULL) {
	  if (usb_tx_high_queue != NULL) {
		  (void) xQueueAddToSet(usb_tx_high_queue, usb_tx_queue_set);
	  }
	  if (usb_tx_low_queue != NULL) {
		  (void) xQueueAddToSet(usb_tx_low_queue, usb_tx_queue_set);
	  }
  }

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
  /* Command_Task has a large switch and several reply builders; 128 words was effectively stack-starved. */
  osThreadDef(CommandTask, Command_Task, osPriorityBelowNormal, 0, 384);
  CommandTaskHandle = osThreadCreate(osThread(CommandTask), NULL);

  /* definition and creation of LEDTask */
  osThreadDef(LEDTask, LED_Task, osPriorityIdle, 0, 256);
  LEDTaskHandle = osThreadCreate(osThread(LEDTask), NULL);

  /* definition and creation of UsbTxTask */
  osThreadDef(UsbTxTask, UsbTx_Task, osPriorityAboveNormal, 0, 256);
  UsbTxTaskHandle = osThreadCreate(osThread(UsbTxTask), NULL);

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
	uint8_t cmd_mai2io[14];
	uint8_t cmd_mai2touch[9];

	HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 0);
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, 1);
	flash_read(Flash.raw_flash);
	if(Flash.system_config != CONFIG_VERSION){
		for(uint8_t i = 0;i<34;i++){
			Flash.touch_threshold[i] = 2000;
		}
		uint8_t touch_sheet_default[34] ={
				0,16,2,3,4,5,6,7,8,
				9,10,11,12,13,14,15,
				1,17,
				18,19,20,21,22,23,24,
				25,26,27,28,29,30,31,32,33
		};
		memcpy(Flash.touch_sheet,touch_sheet_default,34);
		Flash.delay_setting[0] = 0;
		Flash.delay_setting[1] = 0;
		Flash.controller_role = 1;
		Flash.system_config = CONFIG_VERSION;
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
	if ((Flash.controller_role != 1u) && (Flash.controller_role != 2u)) {
		Flash.controller_role = 1u;
		flash_write(Flash.raw_flash);
	}
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
		}
		capsense_update_link_led();
		memset(cmd_mai2io, 0, sizeof(cmd_mai2io));
		memset(cmd_mai2touch, 0, sizeof(cmd_mai2touch));
		cmd_mai2io[0] = 0xff;
		cmd_mai2io[1] = 0x01;
		cmd_mai2io[2] = 0x0A;
		cmd_mai2touch[0] = 0x28;
		cmd_mai2touch[8] = 0x29;

		stack_flow_button(current_button_status);
		for(uint8_t j = 0;j<7;j++){
			for(uint8_t i = 0;i<5;i++){
				if(j == 6 && i == 4){
					break;
					//没有35个触摸点
				}
				if(current_touch_status[i+j*5]){
					cmd_mai2io[j+6] |= (1 << i);
				}
			}
		}
		cmd_mai2io[3] = current_button_status[0] & 0b00001111;
		cmd_mai2io[4] = current_button_status[0] & 0b11110000;
		cmd_mai2io[5] = current_button_status[1];
		for (uint8_t i = 0; i < (uint8_t) (sizeof(cmd_mai2io) - 1u); i++) {
			cmd_mai2io[13] += cmd_mai2io[i];
		}
		if(debug_flag == 0 && !benchmark_quiet_active()){
			if(heart_beat_active()){
				(void) usb_tx_enqueue_low(cmd_mai2io, 14);
			}else if(touch_scan_flag != 0){
				memcpy(cmd_mai2touch+1,cmd_mai2io+6,7);
				(void) usb_tx_enqueue_low(cmd_mai2touch, 9);
			}
		}

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
	uint8_t last_keyboard_buffer[14];
	uint8_t last_hid_buttons0 = 0xFF;
	uint8_t last_hid_io_status = 0xFF;
	memset(keyboard_buffer,0,sizeof(keyboard_buffer));
	memset(last_keyboard_buffer,0,sizeof(last_keyboard_buffer));
	osDelay(1000);
	button_init();
	(void) mai2_hid_buttons_send_report(0, 0);
	last_hid_buttons0 = 0;
	last_hid_io_status = 0;
	while(1){
		osDelay(3);
		button_scan();
		stack_flow_button(current_button_status);
		if (current_button_status[0] != last_hid_buttons0 ||
			current_button_status[1] != last_hid_io_status) {
			if (mai2_hid_buttons_send_report(current_button_status[0], current_button_status[1]) == USBD_OK) {
				last_hid_buttons0 = current_button_status[0];
				last_hid_io_status = current_button_status[1];
			}
		}
		if(!heart_beat_active()){
			for(uint8_t i = 0;i<8;i++){
				keyboard_buffer[i] =  (current_button_status[0] & (1 << i)) ? keyboard_sheet[i] : 0;
			}
			for(uint8_t i = 0;i<6;i++){
				keyboard_buffer[i+8] =  (current_button_status[1] & (1 << i)) ? keyboard_sheet[i+8] : 0;
			}
			if(memcmp(last_keyboard_buffer,keyboard_buffer,14) != 0){
				USBD_HID_Keybaord_SendReport(&hUsbDevice, keyboard_buffer, 14);
				memcpy(last_keyboard_buffer,keyboard_buffer,14);
			}
		}else{
			memset(keyboard_buffer,0,14);
			keyboard_buffer[11] =  (current_button_status[1] & (1 << 3)) ? keyboard_sheet[11] : 0;
			if(memcmp(last_keyboard_buffer,keyboard_buffer,14) != 0){
				USBD_HID_Keybaord_SendReport(&hUsbDevice, keyboard_buffer, 14);
				memcpy(last_keyboard_buffer,keyboard_buffer,14);
			}
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
  serial_frame_t rx_frame;
  benchmark_counter_init();
	for(;;)
  {
	(void) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
	while (serial_command_pop(&rx_frame)) {
		const uint8_t *rxBuffer = rx_frame.data;
		uint8_t rxLen = rx_frame.len;

		if ((rxLen != 0u) && (rxBuffer[0] == 0xFFu)) {
			uint64_t dispatch_cycles;

			if (!serial_binary_frame_valid(rxBuffer, rxLen)) {
				continue;
			}

			dispatch_cycles = benchmark_cycles64();
			switch(rxBuffer[1]){
			case SERIAL_CMD_LED:
//				if(rxBuffer[2] != 27){
//					break;
//				}
//				memcpy(WS2812_data_raw,rxBuffer+3,24);
//				FET_LED_Update(rxBuffer[27],rxBuffer[28],rxBuffer[29]);
				break;
			case SERIAL_CMD_LED_BUTTON: {
				uint8_t speed = 0;
				if(rxBuffer[2] < 24){
					break;
				}
				memcpy(WS2812_data_button,rxBuffer+3,24);
				if (rxBuffer[2] >= 25) {
					speed = rxBuffer[27];
				}
				LED_update_button(speed);
				break;
			}
			case SERIAL_CMD_LED_BILLBOARD:
				if(rxBuffer[2] != 24){
					break;
				}
				memcpy(WS2812_data_billboard,rxBuffer+3,24);
				break;
			case SERIAL_CMD_LED_PWM:
				if(rxBuffer[2] < 3){
					break;
				}
				FET_LED_Update(rxBuffer[3],rxBuffer[4],rxBuffer[5]);
				break;
			case SERIAL_CMD_AUTO_CALIBRATE_THRESHOLD:{
				uint16_t calibrated_thresholds[34];
				uint16_t threshold_min = 0;
				uint16_t threshold_max = 0;
				uint8_t summary_cmd[10] = {0xff, SERIAL_CMD_AUTO_CALIBRATE_THRESHOLD, 6, 0, 34, 0, 0, 0, 0, 0};
				uint8_t success;

				if(rxBuffer[2] != 0){
					break;
				}

				success = capsense_auto_calibrate_thresholds(calibrated_thresholds, &threshold_min, &threshold_max);
				summary_cmd[3] = success;
				memcpy(&summary_cmd[5], &threshold_min, 2);
				memcpy(&summary_cmd[7], &threshold_max, 2);
				for(uint8_t i = 0; i < 9; i++){
					summary_cmd[9] += summary_cmd[i];
				}
				(void) usb_tx_enqueue_high(summary_cmd, 10);

				if(success){
					for(uint8_t start = 0; start < 34; start += 15){
						uint8_t count = (uint8_t) ((34 - start) > 15 ? 15 : (34 - start));
						uint8_t cmd_tmp[40] = {0};

						cmd_tmp[0] = 0xff;
						cmd_tmp[1] = SERIAL_CMD_AUTO_CALIBRATE_THRESHOLD;
						cmd_tmp[2] = (uint8_t) (2 + (count * 2));
						cmd_tmp[3] = start;
						cmd_tmp[4] = count;
						memcpy(&cmd_tmp[5], &calibrated_thresholds[start], count * sizeof(uint16_t));
						for(uint8_t i = 0; i < (uint8_t) (5 + (count * 2)); i++){
							cmd_tmp[5 + (count * 2)] += cmd_tmp[i];
						}
						(void) usb_tx_enqueue_high(cmd_tmp, (uint16_t) (6 + (count * 2)));
					}
				}
				break;
			}
			case SERIAL_CMD_GET_CAPSENSE_UART_STATS:{
				capsense_uart_stats_t stats;
				uint8_t cmd_tmp[40] = {0};
				uint8_t idx = 3;

				if ((rxBuffer[2] > 1) || ((rxBuffer[2] == 1) && (rxBuffer[3] != 1))) {
					break;
				}
				if ((rxBuffer[2] == 1) && (rxBuffer[3] == 1)) {
					capsense_uart_stats_reset();
				}

				capsense_uart_stats_get(&stats);

				cmd_tmp[0] = 0xff;
				cmd_tmp[1] = SERIAL_CMD_GET_CAPSENSE_UART_STATS;
				cmd_tmp[2] = 35;

				memcpy(&cmd_tmp[idx], &stats.checksum_accept_count, sizeof(stats.checksum_accept_count));
				idx += sizeof(stats.checksum_accept_count);
				memcpy(&cmd_tmp[idx], &stats.rolling_checksum_accept_count, sizeof(stats.rolling_checksum_accept_count));
				idx += sizeof(stats.rolling_checksum_accept_count);
				memcpy(&cmd_tmp[idx], &stats.legacy_accept_count, sizeof(stats.legacy_accept_count));
				idx += sizeof(stats.legacy_accept_count);
				memcpy(&cmd_tmp[idx], &stats.short_packet_count, sizeof(stats.short_packet_count));
				idx += sizeof(stats.short_packet_count);
				memcpy(&cmd_tmp[idx], &stats.empty_packet_count, sizeof(stats.empty_packet_count));
				idx += sizeof(stats.empty_packet_count);
				memcpy(&cmd_tmp[idx], &stats.parse_fail_count, sizeof(stats.parse_fail_count));
				idx += sizeof(stats.parse_fail_count);
				memcpy(&cmd_tmp[idx], &stats.uart_error_count, sizeof(stats.uart_error_count));
				idx += sizeof(stats.uart_error_count);
				memcpy(&cmd_tmp[idx], &stats.auto_reset_count, sizeof(stats.auto_reset_count));
				idx += sizeof(stats.auto_reset_count);
				cmd_tmp[idx++] = stats.protocol_version;
				cmd_tmp[idx++] = stats.legacy_payload_offset;
				cmd_tmp[idx++] = stats.rx_failure_streak;

				cmd_tmp[idx] = 0;
				for(uint8_t i = 0; i < idx; i++){
					cmd_tmp[idx] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1));
				break;
			}
			case SERIAL_CMD_GET_USB_CDC_STATS:{
				usb_cdc_tx_stats_t stats;
				uint32_t high_depth = 0;
				uint32_t low_depth = 0;
				uint8_t cmd_tmp[64] = {0};
				uint8_t idx = 3;

				if ((rxBuffer[2] > 1) || ((rxBuffer[2] == 1) && (rxBuffer[3] != 1))) {
					break;
				}
				if ((rxBuffer[2] == 1) && (rxBuffer[3] == 1)) {
					usb_cdc_tx_stats_reset();
				}

				usb_cdc_tx_stats_snapshot(&stats, &high_depth, &low_depth);

				cmd_tmp[0] = 0xff;
				cmd_tmp[1] = SERIAL_CMD_GET_USB_CDC_STATS;
				cmd_tmp[2] = 56;

				memcpy(&cmd_tmp[idx], &stats.high_enqueued_count, sizeof(stats.high_enqueued_count));
				idx += sizeof(stats.high_enqueued_count);
				memcpy(&cmd_tmp[idx], &stats.low_enqueued_count, sizeof(stats.low_enqueued_count));
				idx += sizeof(stats.low_enqueued_count);
				memcpy(&cmd_tmp[idx], &stats.high_drop_count, sizeof(stats.high_drop_count));
				idx += sizeof(stats.high_drop_count);
				memcpy(&cmd_tmp[idx], &stats.low_drop_count, sizeof(stats.low_drop_count));
				idx += sizeof(stats.low_drop_count);
				memcpy(&cmd_tmp[idx], &stats.tx_ok_count, sizeof(stats.tx_ok_count));
				idx += sizeof(stats.tx_ok_count);
				memcpy(&cmd_tmp[idx], &stats.tx_busy_retry_count, sizeof(stats.tx_busy_retry_count));
				idx += sizeof(stats.tx_busy_retry_count);
				memcpy(&cmd_tmp[idx], &stats.tx_fail_retry_count, sizeof(stats.tx_fail_retry_count));
				idx += sizeof(stats.tx_fail_retry_count);
				memcpy(&cmd_tmp[idx], &stats.tx_giveup_count, sizeof(stats.tx_giveup_count));
				idx += sizeof(stats.tx_giveup_count);
				memcpy(&cmd_tmp[idx], &stats.last_tx_latency_ms, sizeof(stats.last_tx_latency_ms));
				idx += sizeof(stats.last_tx_latency_ms);
				memcpy(&cmd_tmp[idx], &stats.max_tx_latency_ms, sizeof(stats.max_tx_latency_ms));
				idx += sizeof(stats.max_tx_latency_ms);
				memcpy(&cmd_tmp[idx], &stats.last_retry_count, sizeof(stats.last_retry_count));
				idx += sizeof(stats.last_retry_count);
				memcpy(&cmd_tmp[idx], &stats.max_retry_count, sizeof(stats.max_retry_count));
				idx += sizeof(stats.max_retry_count);
				memcpy(&cmd_tmp[idx], &high_depth, sizeof(high_depth));
				idx += sizeof(high_depth);
				memcpy(&cmd_tmp[idx], &low_depth, sizeof(low_depth));
				idx += sizeof(low_depth);

				cmd_tmp[idx] = 0;
				for(uint8_t i = 0; i < idx; i++){
					cmd_tmp[idx] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1));
				break;
			}
			case SERIAL_CMD_GET_CONTROLLER_ROLE:{
				uint16_t pid = USBD_GetControllerPid();
				uint8_t cmd_tmp[7] = {
					0xff,
					SERIAL_CMD_GET_CONTROLLER_ROLE,
					3,
					controller_role_normalize(Flash.controller_role),
					(uint8_t) (pid & 0xFFu),
					(uint8_t) ((pid >> 8) & 0xFFu),
					0
				};

				for(uint8_t i = 0; i < 6; i++){
					cmd_tmp[6] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
				break;
			}
			case SERIAL_CMD_SET_CONTROLLER_ROLE:{
				uint8_t requested_role;
				uint8_t applied_role;
				uint8_t ok = 0;
				uint8_t cmd_tmp[6] = {0xff, SERIAL_CMD_SET_CONTROLLER_ROLE, 2, 0, 0, 0};

				if(rxBuffer[2] != 1){
					break;
				}

				requested_role = rxBuffer[3];
				if ((requested_role == 1u) || (requested_role == 2u)) {
					applied_role = controller_role_normalize(requested_role);
					Flash.controller_role = applied_role;
					player = applied_role;
					USBD_SetControllerRole(applied_role);
					flash_write(Flash.raw_flash);
					ok = 1u;
					cmd_tmp[3] = applied_role;
					cmd_tmp[4] = ok;
				} else {
					cmd_tmp[3] = requested_role;
					cmd_tmp[4] = ok;
				}

				for(uint8_t i = 0; i < 5; i++){
					cmd_tmp[5] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));

				if (ok != 0u) {
					osDelay(100);
					__disable_irq();
					NVIC_SystemReset();
				}
				break;
			}
			case SERIAL_CMD_SCAN_START:
				if(rxBuffer[2] != 0){
					break;
				}
				break;
			case SERIAL_CMD_SCAN_STOP:
				if(rxBuffer[2] != 0){
					break;
				}
				break;
			case SERIAL_CMD_READ_MONO_THRESHOLD:{
				if(rxBuffer[2] != 1){
					break;
				}
				uint8_t cmd_tmp[7] = {0xff,5,3,0,0,0,0};
				cmd_tmp[3] = rxBuffer[3];
				memcpy(cmd_tmp + 4,&Flash.touch_threshold[cmd_tmp[3]],2);
				for(uint8_t i = 0;i<6;i++){
					cmd_tmp[6] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, 7);
				break;
			}

			case SERIAL_CMD_WRITE_MONO_THRESHOLD:{
				if(rxBuffer[2] != 3){
					break;
				}
				memcpy(&Flash.touch_threshold[rxBuffer[3]],&rxBuffer[4],2);
				flash_write(Flash.raw_flash);
				uint8_t cmd_tmp[5] = {0xff,6,1,1,7};
				(void) usb_tx_enqueue_high(cmd_tmp, 5);
				break;
			}
			case SERIAL_CMD_READ_TOUCH_SHEET:{
				if(rxBuffer[2] != 0){
					break;
				}
				uint8_t cmd_tmp[38] = {0xff,7,34};
				for(uint8_t i = 0;i<34;i++){
					cmd_tmp[i + 3] = Flash.touch_sheet[i];
				}
//				memcpy(cmd_tmp + 3,Flash.touch_sheet,34);
				for(uint8_t i = 0;i<37;i++){
					cmd_tmp[37] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, 38);
				break;
			}
			case SERIAL_CMD_WRITE_TOUCH_SHEET:{
				if(rxBuffer[2] != 34){
					break;
				}
				//memcpy(Flash.touch_sheet,&rxBuffer[3],34);
				for(uint8_t i = 0;i<34;i++){
					Flash.touch_sheet[i] = rxBuffer[i+3];
				}
				flash_write(Flash.raw_flash);
				uint8_t cmd_tmp[5] = {0xff,8,1,1,9};
				(void) usb_tx_enqueue_high(cmd_tmp, 5);
				break;
			}
			case SERIAL_CMD_CALIBRATION_BEGIN:{
				if(rxBuffer[2] != 0){
					break;
				}
				capsense_calibration_begin();
				{
					uint8_t cmd_tmp[5] = {0xff, SERIAL_CMD_CALIBRATION_BEGIN, 1, 1, 0};
					for(uint8_t i = 0; i < 4; i++){
						cmd_tmp[4] += cmd_tmp[i];
					}
					(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
				}
				break;
			}
			case SERIAL_CMD_CALIBRATION_CAPTURE:{
				capsense_calibration_result_t result = {0};
				uint8_t capture_flags = 0u;
				uint8_t ok = 0u;
				uint8_t cmd_tmp[14] = {
					0xff,
					SERIAL_CMD_CALIBRATION_CAPTURE,
					10,
					0,
					0,
					0xFF,
					0,
					0,
					0,
					0,
					0,
					0,
					0,
					0
				};

				if((rxBuffer[2] != 1u) && (rxBuffer[2] != 2u)){
					break;
				}

				if(rxBuffer[2] == 2u){
					capture_flags = rxBuffer[4];
				}

				ok = capsense_calibration_capture(rxBuffer[3], capture_flags, &result);
				cmd_tmp[3] = ok;
				cmd_tmp[4] = rxBuffer[3];
				if(ok != 0u){
					cmd_tmp[5] = result.best_channel;
					cmd_tmp[6] = result.confidence;
					memcpy(&cmd_tmp[7], &result.threshold, 2);
					memcpy(&cmd_tmp[9], &result.peak_delta, 2);
					memcpy(&cmd_tmp[11], &result.idle_threshold, 2);
				}

				for(uint8_t i = 0; i < 13; i++){
					cmd_tmp[13] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
				break;
			}
			case SERIAL_CMD_CALIBRATION_COMMIT:{
				uint8_t ok;

				if(rxBuffer[2] != 0){
					break;
				}

				ok = capsense_calibration_commit();
				{
					uint8_t cmd_tmp[5] = {0xff, SERIAL_CMD_CALIBRATION_COMMIT, 1, ok, 0};
					for(uint8_t i = 0; i < 4; i++){
						cmd_tmp[4] += cmd_tmp[i];
					}
					(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
				}
				break;
			}
			case SERIAL_CMD_CALIBRATION_ABORT:{
				if(rxBuffer[2] != 0){
					break;
				}

				capsense_calibration_abort();
				{
					uint8_t cmd_tmp[5] = {0xff, SERIAL_CMD_CALIBRATION_ABORT, 1, 1, 0};
					for(uint8_t i = 0; i < 4; i++){
						cmd_tmp[4] += cmd_tmp[i];
					}
					(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
				}
				break;
			}
			case SERIAL_CMD_CALIBRATION_CANCEL_CAPTURE:{
				if(rxBuffer[2] != 0u){
					break;
				}

				{
					uint8_t cmd_tmp[5] = {0xff, SERIAL_CMD_CALIBRATION_CANCEL_CAPTURE, 1, 1, 0};
					for(uint8_t i = 0; i < 4; i++){
						cmd_tmp[4] += cmd_tmp[i];
					}
					(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
				}
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
					(void) usb_tx_enqueue_high(cmd_tmp, 6);
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
					(void) usb_tx_enqueue_high(cmd_tmp, 5);
					break;
				}
				break;
			}
			case SERIAL_CMD_RESET:{
				if(rxBuffer[2] != 0){
					break;
				}
				// Send acknowledge before reset
				uint8_t cmd_tmp[5] = {0xff,0x10,1,1,0x11};
				(void) usb_tx_enqueue_high(cmd_tmp, 5);
				// Wait for transmission to complete
				osDelay(100);
				// Perform software reset
				__disable_irq();
				NVIC_SystemReset();
				break;
			}
			case SERIAL_CMD_JUMP_TO_DFU:{
				if(rxBuffer[2] != 0){
					break;
				}
				// Send acknowledge before jumping to DFU
				uint8_t cmd_tmp[5] = {0xff,0x21,1,1,0x22};
				(void) usb_tx_enqueue_high(cmd_tmp, 5);
				// Wait for transmission to complete
				osDelay(200);  // Increased delay
				
				// Request DFU mode and reset (more reliable method)
				Request_DFU_Mode_And_Reset();
				break;
			}
			case SERIAL_CMD_JUMP_TO_BOOTLOADER:{
				if(rxBuffer[2] != 0){
					break;
				}
				uint8_t cmd_tmp[5] = {0xff, SERIAL_CMD_JUMP_TO_BOOTLOADER, 1, 1, 0};
				for(uint8_t i = 0; i < 4; i++){
					cmd_tmp[4] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
				osDelay(200);
				Request_Affine_Bootloader_And_Reset();
				break;
			}
			case SERIAL_CMD_BENCHMARK:{
				uint16_t expected_len = (uint16_t) rxBuffer[2] + 4;
				if (rxBuffer[2] > BENCHMARK_MAX_PAYLOAD || rxLen < expected_len) {
					break;
				}
				benchmark_quiet_until_ms = HAL_GetTick() + BENCHMARK_QUIET_PERIOD_MS;
				serial_send_benchmark_reply(SERIAL_CMD_BENCHMARK, rxBuffer + 3, rxBuffer[2], dispatch_cycles);
				break;
			}
			case SERIAL_CMD_BENCHMARK_EVENT:{
				uint32_t delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
				if (rxBuffer[2] != BENCHMARK_EVENT_PAYLOAD) {
					break;
				}
				delay_ms = benchmark_read_u32_le(rxBuffer + 7);
				if (delay_ms == 0) {
					delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
				}
				taskENTER_CRITICAL();
				benchmark_event_sequence = benchmark_read_u32_le(rxBuffer + 3);
				benchmark_event_due_ms = HAL_GetTick() + delay_ms;
				benchmark_event_transport = 1;
				benchmark_event_pending = 1;
				taskEXIT_CRITICAL();
				benchmark_quiet_until_ms = HAL_GetTick() + delay_ms + BENCHMARK_QUIET_PERIOD_MS;
				break;
			}
			case SERIAL_CMD_BENCHMARK_HID_EVENT:{
				uint32_t delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
				if (rxBuffer[2] != BENCHMARK_EVENT_PAYLOAD) {
					break;
				}
				delay_ms = benchmark_read_u32_le(rxBuffer + 7);
				if (delay_ms == 0) {
					delay_ms = BENCHMARK_EVENT_DELAY_MS_DEFAULT;
				}
				serial_send_benchmark_reply(SERIAL_CMD_BENCHMARK_HID_EVENT, rxBuffer + 3, rxBuffer[2], dispatch_cycles);
				taskENTER_CRITICAL();
				benchmark_event_sequence = benchmark_read_u32_le(rxBuffer + 3);
				benchmark_event_due_ms = HAL_GetTick() + delay_ms;
				benchmark_event_transport = 2;
				benchmark_event_pending = 1;
				taskEXIT_CRITICAL();
				benchmark_quiet_until_ms = HAL_GetTick() + delay_ms + BENCHMARK_QUIET_PERIOD_MS;
				break;
			}
			case SERIAL_CMD_HEART_BEAT:
				if(rxBuffer[2] != 0){
					break;
				}
				heart_beat_refresh();
				break;
			case SERIAL_CMD_TO_DEBUG_MODE:
				{
					uint8_t requested_mode = SERIAL_DEBUG_STREAM_MODE_FOCUS;
					uint8_t ok = 0u;
					uint8_t ack_cmd[5] = {0xff, SERIAL_CMD_TO_DEBUG_MODE, 1, 0, 0};
					uint8_t status_cmd[6] = {0xff, SERIAL_CMD_TO_DEBUG_MODE, 2, 2, capsense_data_ready, 0};

					if(rxBuffer[2] == 0u){
						ok = 1u;
					}else if((rxBuffer[2] == 1u) &&
							(rxBuffer[3] <= SERIAL_DEBUG_STREAM_MODE_RAW_34)){
						requested_mode = rxBuffer[3];
						ok = 1u;
					}

					if (ok != 0u) {
						debug_stream_mode = requested_mode;
						debug_flag = 1;
					}
					ack_cmd[3] = ok;
					for(uint8_t i = 0; i < 4; i++){
						ack_cmd[4] += ack_cmd[i];
					}
					if (ok != 0u) {
						for(uint8_t i = 0; i < 5; i++){
							status_cmd[5] += status_cmd[i];
						}
					}
					(void) usb_tx_enqueue_high(ack_cmd, 5);
					if (ok != 0u) {
						(void) usb_tx_enqueue_high(status_cmd, 6);
					}
				}
				break;
			case SERIAL_CMD_SET_DEBUG_CHANNEL:{
				uint8_t cmd_tmp[6] = {0xff, SERIAL_CMD_SET_DEBUG_CHANNEL, 2, 0, 0, 0};
				if(rxBuffer[2] != 1){
					cmd_tmp[4] = 0;
				}else if(rxBuffer[3] < 34){
					debug_channel = rxBuffer[3];
					cmd_tmp[3] = debug_channel;
					cmd_tmp[4] = 1;
				}else{
					cmd_tmp[3] = rxBuffer[3];
					cmd_tmp[4] = 0;
				}
				for(uint8_t i = 0;i<5;i++){
					cmd_tmp[5] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, 6);
				break;
			}
			case SERIAL_CMD_EXIT_DEBUG_MODE:{
				if(rxBuffer[2] != 0u){
					break;
				}

				debug_flag = 0u;
				debug_stream_mode = SERIAL_DEBUG_STREAM_MODE_FOCUS;
				{
					uint8_t ack_cmd[5] = {0xff, SERIAL_CMD_EXIT_DEBUG_MODE, 1, 1, 0};
					for(uint8_t i = 0; i < 4; i++){
						ack_cmd[4] += ack_cmd[i];
					}
					(void) usb_tx_enqueue_high(ack_cmd, 5);
				}
				break;
			}
			case SERIAL_CMD_GET_BOARD_INFO:{
				if(rxBuffer[2] != 0){
					break;
				}
				char board_name[] = "1020-050201";
				uint8_t name_len = strlen(board_name);
				uint8_t version_len = strlen(VERSION);
				
				uint32_t uid[3];
				uid[0] = HAL_GetUIDw0();
				uid[1] = HAL_GetUIDw1();
				uid[2] = HAL_GetUIDw2();
				uint8_t uid_len = 12;

				uint8_t data_len = 1 + version_len + 1 + name_len + 1 + uid_len;
				uint8_t cmd_tmp[64];
				
				cmd_tmp[0] = 0xFF;
				cmd_tmp[1] = SERIAL_CMD_GET_BOARD_INFO;
				cmd_tmp[2] = data_len;
				
				uint8_t idx = 3;
				cmd_tmp[idx++] = version_len;
				memcpy(&cmd_tmp[idx], VERSION, version_len);
				idx += version_len;
				
				cmd_tmp[idx++] = name_len;
				memcpy(&cmd_tmp[idx], board_name, name_len);
				idx += name_len;

				cmd_tmp[idx++] = uid_len;
				memcpy(&cmd_tmp[idx], uid, uid_len);
				idx += uid_len;
				
				cmd_tmp[idx] = 0;
				for(uint8_t i = 0; i < idx; i++){
					cmd_tmp[idx] += cmd_tmp[i];
				}
				(void) usb_tx_enqueue_high(cmd_tmp, idx + 1);
				break;
			}
		}
		}else if((rxLen == 6u) && (rxBuffer[0] == 0x7B)){
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
				(void) usb_tx_enqueue_high((uint8_t*)cmd_tmp,6);
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
				(void) usb_tx_enqueue_high((uint8_t*)cmd_tmp,6);
				break;
			default:
				break;
		}
	}
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
	for(uint8_t i = 0;i<8;i++){
		LED_set(i,0xff,0xff,0xff);
	}
	LED_refresh();
	while(1){
		LED_Task_ProcessPending();
		LED_Fade_IRQHandler();
		osDelay(1);
	}
  /* USER CODE END LED_Task */
}

void UsbTx_Task(void const * argument)
{
  /* USER CODE BEGIN UsbTx_Task */
	usb_tx_packet_t packet;
	QueueSetMemberHandle_t ready = NULL;
	(void) argument;

	for(;;){
		if (usb_tx_queue_set == NULL) {
			osDelay(1);
			continue;
		}

		ready = xQueueSelectFromSet(usb_tx_queue_set, portMAX_DELAY);
		if (ready == NULL) {
			continue;
		}

		if (usb_tx_high_queue != NULL &&
			xQueueReceive(usb_tx_high_queue, &packet, 0) == pdPASS) {
			/* High-priority control traffic preempts streamed touch packets. */
		} else if (ready == usb_tx_low_queue) {
			if (xQueueReceive(usb_tx_low_queue, &packet, 0) != pdPASS) {
				continue;
			}
		} else if (ready == usb_tx_high_queue) {
			if (xQueueReceive(usb_tx_high_queue, &packet, 0) != pdPASS) {
				continue;
			}
		} else {
			continue;
		}

		{
			uint32_t retry_count = 0;
			uint8_t tx_ok = 0;

			for (uint8_t attempt = 0; attempt < BENCHMARK_TX_RETRY_COUNT; attempt++) {
				uint8_t tx_result = CDC_Transmit(0, packet.data, packet.len);

				if (tx_result == USBD_OK) {
					uint32_t tx_latency_ms = HAL_GetTick() - packet.enqueue_tick;

					usb_cdc_tx_stats.tx_ok_count++;
					usb_cdc_tx_stats.last_tx_latency_ms = tx_latency_ms;
					usb_cdc_tx_stats.last_retry_count = retry_count;
					if (tx_latency_ms > usb_cdc_tx_stats.max_tx_latency_ms) {
						usb_cdc_tx_stats.max_tx_latency_ms = tx_latency_ms;
					}
					if (retry_count > usb_cdc_tx_stats.max_retry_count) {
						usb_cdc_tx_stats.max_retry_count = retry_count;
					}
					tx_ok = 1;
					break;
				}

				retry_count++;
				if (tx_result == USBD_BUSY) {
					usb_cdc_tx_stats.tx_busy_retry_count++;
				} else {
					usb_cdc_tx_stats.tx_fail_retry_count++;
				}
				osDelay(1);
			}

			if (!tx_ok) {
				uint32_t tx_latency_ms = HAL_GetTick() - packet.enqueue_tick;

				usb_cdc_tx_stats.tx_giveup_count++;
				usb_cdc_tx_stats.last_tx_latency_ms = tx_latency_ms;
				usb_cdc_tx_stats.last_retry_count = retry_count;
				if (tx_latency_ms > usb_cdc_tx_stats.max_tx_latency_ms) {
					usb_cdc_tx_stats.max_tx_latency_ms = tx_latency_ms;
				}
				if (retry_count > usb_cdc_tx_stats.max_retry_count) {
					usb_cdc_tx_stats.max_retry_count = retry_count;
				}
			}
		}
	}
  /* USER CODE END UsbTx_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

