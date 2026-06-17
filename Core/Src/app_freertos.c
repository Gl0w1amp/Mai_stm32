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
#include "input_snapshot.h"
#include "serial_commands.h"
#include "serial_reports.h"
#include "usbd_desc.h"
#include "usb_reporter.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
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
#define CAPSENSE_LED_ONLINE_HOLD_MS 300u
#define CAPSENSE_LED_ERROR_HOLD_MS 500u
#define CAPSENSE_LED_WAIT_BLINK_HALF_PERIOD_MS 500u
#define CAPSENSE_LED_ERROR_BLINK_HALF_PERIOD_MS 125u
#define HEART_BEAT_HOLD_MS 300u
#define DEBUG_EXIT_RESET_DELAY_MS 500u
#define DEBUG_EXIT_USB_DISCONNECT_HOLD_MS 500u
#define RAW_DEBUG_SNAPSHOT_PARTS 2u
#define RAW_DEBUG_SNAPSHOT_VALUES_PER_PART (34u / RAW_DEBUG_SNAPSHOT_PARTS)
#define TOUCH_CHANNEL_COUNT 34u
#define TOUCH_THRESHOLD_DEFAULT 2000u
#define DELAY_SETTING_COUNT 2u
#define DELAY_SETTING_MAX 9u
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
volatile uint8_t debug_exit_reset_pending = 0u;
volatile uint32_t debug_exit_reset_deadline_ms = 0u;
static uint32_t benchmark_last_cycles = 0;
static uint32_t benchmark_cycles_high = 0;
static const uint8_t touch_sheet_default[TOUCH_CHANNEL_COUNT] = {
		0,16,2,3,4,5,6,7,8,
		9,10,11,12,13,14,15,
		1,17,
		18,19,20,21,22,23,24,
		25,26,27,28,29,30,31,32,33
};
/* USER CODE END Variables */
osThreadId TouchTaskHandle;
osThreadId ButtonTaskHandle;
osThreadId CommandTaskHandle;
osThreadId LEDTaskHandle;
osThreadId UsbTxTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void benchmark_counter_init(void);
static uint32_t benchmark_cycles(void);
uint64_t benchmark_cycles64(void);
static void benchmark_write_u32_le(uint8_t *dst, uint32_t value);
static void benchmark_write_u64_le(uint8_t *dst, uint64_t value);
uint32_t benchmark_read_u32_le(const uint8_t *src);
static uint8_t benchmark_quiet_active(void);
static void capsense_update_link_led(void);
static uint8_t usb_tx_enqueue_high(const uint8_t *buf, uint16_t len);
static uint8_t usb_tx_enqueue_low(const uint8_t *buf, uint16_t len);
uint8_t serial_send_raw_debug_snapshot(uint8_t sequence, uint8_t part_index);
void usb_cdc_tx_stats_reset(void);
void usb_cdc_tx_stats_snapshot(usb_cdc_tx_stats_t *stats_out, uint32_t *high_depth_out, uint32_t *low_depth_out);
void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, uint64_t dispatch_cycles);
static void serial_send_benchmark_event(uint32_t sequence, uint64_t event_cycles);
static void hid_send_benchmark_event(uint32_t sequence, uint64_t event_cycles);
static void benchmark_emit_pending_event(void);
void heart_beat_refresh(void);
static uint8_t heart_beat_active(void);
uint8_t controller_role_normalize(uint8_t role);
static void flash_load_defaults(void);
uint8_t flash_touch_sheet_valid(const uint8_t *sheet);
static uint8_t flash_config_sanitize(void);
void serial_send_simple_status(uint8_t command, uint8_t ok);
static void debug_exit_reset_now(void);
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

uint64_t benchmark_cycles64(void)
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

uint32_t benchmark_read_u32_le(const uint8_t *src)
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

void heart_beat_refresh(void)
{
	heart_beat_deadline_ms = HAL_GetTick() + HEART_BEAT_HOLD_MS;
}

static uint8_t heart_beat_active(void)
{
	return ((int32_t)(heart_beat_deadline_ms - HAL_GetTick()) > 0) ? 1u : 0u;
}

uint8_t controller_role_normalize(uint8_t role)
{
	return (role == 2u) ? 2u : 1u;
}

static void flash_load_defaults(void)
{
	for(uint8_t i = 0;i<TOUCH_CHANNEL_COUNT;i++){
		Flash.touch_threshold[i] = TOUCH_THRESHOLD_DEFAULT;
	}
	memcpy(Flash.touch_sheet, touch_sheet_default, TOUCH_CHANNEL_COUNT);
	Flash.delay_setting[0] = 0u;
	Flash.delay_setting[1] = 0u;
	Flash.controller_role = 1u;
	Flash.system_config = CONFIG_VERSION;
}

uint8_t flash_touch_sheet_valid(const uint8_t *sheet)
{
	if (sheet == NULL) {
		return 0u;
	}
	for(uint8_t i = 0;i<TOUCH_CHANNEL_COUNT;i++){
		if (sheet[i] >= TOUCH_CHANNEL_COUNT) {
			return 0u;
		}
	}
	return 1u;
}

static uint8_t flash_config_sanitize(void)
{
	uint8_t changed = 0u;

	if(Flash.system_config != CONFIG_VERSION){
		flash_load_defaults();
		return flash_write(Flash.raw_flash);
	}

	if (flash_touch_sheet_valid(Flash.touch_sheet) == 0u) {
		memcpy(Flash.touch_sheet, touch_sheet_default, TOUCH_CHANNEL_COUNT);
		changed = 1u;
	}
	for(uint8_t i = 0;i<DELAY_SETTING_COUNT;i++){
		if(Flash.delay_setting[i] > DELAY_SETTING_MAX){
			Flash.delay_setting[i] = DELAY_SETTING_MAX;
			changed = 1u;
		}
	}
	if ((Flash.controller_role != 1u) && (Flash.controller_role != 2u)) {
		Flash.controller_role = 1u;
		changed = 1u;
	}

	if (changed != 0u) {
		return flash_write(Flash.raw_flash);
	}
	return 1u;
}

void serial_send_simple_status(uint8_t command, uint8_t ok)
{
	uint8_t cmd_tmp[5] = {0xff, command, 1u, ok, 0u};

	for(uint8_t i = 0;i<4u;i++){
		cmd_tmp[4] += cmd_tmp[i];
	}
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, sizeof(cmd_tmp));
}

static void debug_exit_reset_now(void)
{
	debug_exit_reset_pending = 0u;
	/* Keep USB offline long enough for Windows CDC to retire the current
	 * devnode before the MCU comes back and re-enumerates.
	 */
	(void) USBD_Stop(&hUsbDevice);
	(void) USBD_DeInit(&hUsbDevice);
	osDelay(DEBUG_EXIT_USB_DISCONNECT_HOLD_MS);
	NVIC_SystemReset();
}

void usb_cdc_tx_stats_reset(void)
{
	usb_reporter_cdc_stats_reset();
}

void usb_cdc_tx_stats_snapshot(usb_cdc_tx_stats_t *stats_out, uint32_t *high_depth_out, uint32_t *low_depth_out)
{
	usb_reporter_cdc_stats_snapshot(stats_out, high_depth_out, low_depth_out);
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

static uint8_t usb_tx_enqueue_high(const uint8_t *buf, uint16_t len)
{
	return usb_reporter_cdc_enqueue_high(buf, len);
}

static uint8_t usb_tx_enqueue_low(const uint8_t *buf, uint16_t len)
{
	return usb_reporter_cdc_enqueue_low(buf, len);
}

uint8_t serial_cdc_tx_enqueue_high(const uint8_t *buf, uint16_t len)
{
	/* Keep command replies off CDC IN; CDC IN is reserved for legacy/live output. */
	return usb_reporter_vendor_hid_enqueue(buf, len);
}

uint8_t serial_cdc_tx_enqueue_high_isr(const uint8_t *buf, uint16_t len)
{
	return usb_reporter_cdc_enqueue_high_isr(buf, len);
}

uint8_t serial_cdc_tx_enqueue_low(const uint8_t *buf, uint16_t len)
{
	return usb_tx_enqueue_low(buf, len);
}

uint32_t serial_cdc_tx_low_spaces_available(void)
{
	return usb_reporter_cdc_low_spaces_available();
}

uint8_t serial_send_raw_debug_snapshot(uint8_t sequence, uint8_t part_index)
{
	uint8_t cmd_tmp[1u + 1u + 1u + 1u + 1u + 1u +
			(RAW_DEBUG_SNAPSHOT_VALUES_PER_PART * 2u) + 1u] = {0};
	uint8_t idx = 0u;
	uint8_t first_channel = (uint8_t) (part_index * RAW_DEBUG_SNAPSHOT_VALUES_PER_PART);

	if (part_index >= RAW_DEBUG_SNAPSHOT_PARTS) {
		return 0u;
	}

	cmd_tmp[idx++] = 0xFFu;
	cmd_tmp[idx++] = SERIAL_CMD_GET_RAW_DEBUG_SNAPSHOT;
	cmd_tmp[idx++] = (uint8_t) (3u + (RAW_DEBUG_SNAPSHOT_VALUES_PER_PART * 2u));
	cmd_tmp[idx++] = sequence;
	cmd_tmp[idx++] = part_index;
	cmd_tmp[idx++] = RAW_DEBUG_SNAPSHOT_PARTS;

	for (uint8_t channel = first_channel;
			channel < (uint8_t) (first_channel + RAW_DEBUG_SNAPSHOT_VALUES_PER_PART);
			channel++) {
		uint16_t raw = Touch.channel_raw[channel];
		cmd_tmp[idx++] = (uint8_t) (raw & 0xFFu);
		cmd_tmp[idx++] = (uint8_t) ((raw >> 8) & 0xFFu);
	}

	for (uint8_t checksum_index = 0u; checksum_index < idx; checksum_index++) {
		cmd_tmp[idx] += cmd_tmp[checksum_index];
	}

	return serial_cdc_tx_enqueue_high(cmd_tmp, (uint16_t) (idx + 1u));
}

/* Echoes the benchmark payload and attaches device-side cycle timestamps. */
void serial_send_benchmark_reply(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, uint64_t dispatch_cycles)
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
	(void) serial_cdc_tx_enqueue_high(cmd_tmp, idx + 1);
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
	for(uint8_t i = 0;i<8;i++){
		LED_set(i,0xff,0xff,0xff);
	}
	LED_refresh();
	while(1){
		LED_Task_ProcessPending();
		LED_Fade_IRQHandler();
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

