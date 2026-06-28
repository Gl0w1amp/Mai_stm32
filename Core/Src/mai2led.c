#include "mai2led.h"
#include "led_ws2812.h"
#include "led_state.h"
#include "led_fet.h"
#include "usart.h"
#include "critical_section.h"
#include <string.h>
#include <stdbool.h>

#define NUM_LED 16
#define PRE_BUTTON_LED 2
#define BUTTON_LED_COUNT (NUM_LED / PRE_BUTTON_LED)
#define LED_RX_QUEUE_LENGTH 4u

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;
uint8_t led_write_buffer[64];
uint8_t dummyEEPRom[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
PacketReq req;
PacketRes res;

//uint8_t mai_led_default_response[8] = {0xe0,0x01,0x11,0x03,0x01,0x31,0x01,0x48};
//uint8_t mai_led_eeprom_response[9] = {0xe0,0x01 ,0x11,0x04,0x01,0x7c,0x01,0x00,0x94};
//uint8_t mai_led_boardinfo_response[18] = {0xe0,0x01,0x11,0x0d,0x01,0xf0,0x01,0x31,0x35,0x30,0x37,0x30,0x2d,0x30,0x34,0xff,0x90,0x2e};
//uint8_t mai_led_boardstatus_response[12] = {0xe0,0x01,0x11,0x07,0x01,0xf1,0x01,00,00,00,00,0x0c};
//uint8_t mai_led_protocolversion_response[11] = {0xe0,0x01,0x11,0x06,0x01,0xf3,0x01,0x01,0x00,0x00,0x0e};

uint8_t WS2812_data_raw[24] = {
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};
uint8_t led_uart_buffer_rx[64];
uint8_t led_uart_buffer_tx[64];

typedef struct {
    uint8_t len;
    uint8_t data[64];
} LedRxFrame;

static LedRxFrame led_rx_queue[LED_RX_QUEUE_LENGTH];
static volatile uint8_t led_rx_head = 0;
static volatile uint8_t led_rx_tail = 0;
static volatile uint8_t led_rx_count = 0;
static volatile uint8_t led_uart_rx_restart_pending = 0u;

static uint8_t led_uart_start_receive_to_idle(void)
{
	HAL_StatusTypeDef status;

	status = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, led_uart_buffer_rx, 64);
	if (status != HAL_OK) {
		return 0u;
	}

	__HAL_DMA_DISABLE_IT(&hdma_usart1_rx, DMA_IT_HT);
	return 1u;
}

static uint8_t led_rx_frame_pop(LedRxFrame *frame)
{
    uint32_t primask;

    if (frame == NULL) {
        return 0;
    }

    primask = critical_section_enter();
    if (led_rx_count == 0u) {
        critical_section_exit(primask);
        return 0;
    }

    *frame = led_rx_queue[led_rx_tail];
    led_rx_tail = (uint8_t) ((led_rx_tail + 1u) % LED_RX_QUEUE_LENGTH);
    led_rx_count--;
    critical_section_exit(primask);

    return 1;
}

void LED_UART_Init(){
	led_ws2812_reset();
	led_rx_head = 0;
	led_rx_tail = 0;
	led_rx_count = 0;
	led_uart_rx_restart_pending = 0u;
	if (led_uart_start_receive_to_idle() == 0u) {
		led_uart_rx_restart_pending = 1u;
	}
}

void LED_UART_RequestRxRestart(void)
{
	led_uart_rx_restart_pending = 1u;
}

uint8_t led_packet_check(uint8_t* data, uint8_t len, uint8_t* consumed) {
	bool escape = false;
	uint8_t raw_pos = 0;
	uint8_t req_pos = 0;
	uint8_t checksum = 0;
	uint8_t expected_len = 0xFF;
	*consumed = 0;

	/* find sync */
	while(raw_pos < len && data[raw_pos] != Sync){
		raw_pos++;
	}
	if(raw_pos >= len){
		*consumed = len;
		return 0;
	}
	raw_pos++; // skip Sync

	/* decode payload */
	while(raw_pos < len){
		if(data[raw_pos] == Marker){
			escape = true;
			raw_pos++;
			continue;
		}
		uint8_t byte = data[raw_pos];
		if(escape){
			byte += 1;
			escape = false;
		}
		req.bytes[req_pos] = byte;
		checksum += req.bytes[req_pos];
		raw_pos++;
		req_pos++;

		if(req_pos == 3){
			expected_len = req.length;
			/* sanity: avoid buffer overflow */
			if(expected_len > sizeof(req.bytes) - 4){
				*consumed = raw_pos;
				return 0;
			}
		}

		if(expected_len != 0xFF && req_pos == expected_len + 3){
			break; // next byte is checksum
		}
	}

	if(raw_pos >= len || expected_len == 0xFF){
		*consumed = raw_pos;
		return 0;
	}

	/* read checksum (supports escaped checksum) */
	if(data[raw_pos] == Marker){
		raw_pos++;
		if(raw_pos >= len){
			*consumed = raw_pos;
			return 0;
		}
		req.bytes[req_pos] = data[raw_pos] + 1;
	}else{
		req.bytes[req_pos] = data[raw_pos];
	}
	raw_pos++;
	*consumed = raw_pos;

	return (checksum == req.bytes[expected_len + 3]) ? req.cmd : 0;
}

void led_packet_write() {
  uint8_t checksum = 0, len = 0;
  if (res.cmd == 0) {
    return;
  }
  memset(led_write_buffer,0,sizeof(led_write_buffer));
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
  memset(res.bytes,0,sizeof(res.bytes));
}

void res_init(uint8_t length, uint8_t status, uint8_t report) {
  res.dstNodeID = req.srcNodeID;
  res.srcNodeID = req.dstNodeID;
  res.length = 3 + length;
  res.status = status;
  res.cmd = req.cmd;
  res.report = report;
}

uint8_t LED_RxFramePush(const uint8_t *data, uint16_t len)
{
    uint32_t primask;
    LedRxFrame *frame;

    if ((data == NULL) || (len == 0u) || (len > sizeof(led_uart_buffer_rx))) {
        return 0;
    }

    primask = critical_section_enter();
    if (led_rx_count >= LED_RX_QUEUE_LENGTH) {
        critical_section_exit(primask);
        return 0;
    }

    frame = &led_rx_queue[led_rx_head];
    frame->len = (uint8_t) len;
    memcpy(frame->data, data, len);
    led_rx_head = (uint8_t) ((led_rx_head + 1u) % LED_RX_QUEUE_LENGTH);
    led_rx_count++;
    critical_section_exit(primask);

    return 1;
}

void LED_Task_Process(const uint8_t *data, uint16_t len){
	uint8_t offset = 0;
	while(offset < len){
		uint8_t consumed = 0;
		uint8_t cmd = led_packet_check((uint8_t *) data + offset, len - offset, &consumed);
		offset += consumed;
		if(consumed == 0){
			break;
		}
		if(cmd == 0){
			continue;
		}
		switch(cmd){
		case SetLedGs8Bit:
			if (req.index < BUTTON_LED_COUNT) {
				LED_NotifyHostControl(HAL_GetTick());
				set_led_immediate(req.index, req.color[0], req.color[1], req.color[2]);
				res_init(0,AckStatus_Ok,AckReport_Ok);
			} else {
				res_init(0,AckStatus_Ok,AckReport_ParamError);
			}
			break;
		case SetLedGs8BitMulti:
		{
			uint8_t count = resolve_multi_len(req.start, req.end);
			if ((req.start >= BUTTON_LED_COUNT) && (count == 0u)) {
				res_init(0,AckStatus_Ok,AckReport_ParamError);
			} else {
				LED_NotifyHostControl(HAL_GetTick());
				for(uint8_t i = 0; i < count; i++){
					set_led_immediate(req.start + i, req.Multi_color[0], req.Multi_color[1], req.Multi_color[2]);
				}
				res_init(0,AckStatus_Ok,AckReport_Ok);
			}
			break;
		}
		case SetLedGs8BitMultiFade:
		{
			uint8_t count = resolve_multi_len(req.start, req.end);
			if ((req.start >= BUTTON_LED_COUNT) && (count == 0u)) {
				res_init(0,AckStatus_Ok,AckReport_ParamError);
			} else {
				LED_NotifyHostControl(HAL_GetTick());
				for(uint8_t i = 0; i < count; i++){
					schedule_led_fade(req.start + i, req.Multi_color[0], req.Multi_color[1], req.Multi_color[2], req.speed);
				}
				res_init(0,AckStatus_Ok,AckReport_Ok);
			}
			break;
		}
		case SetLedFet:
			FET_LED_Update(req.BodyLed, req.ExtLed, req.SideLed);
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case SetLedGsUpdate:
			LED_NotifyHostControl(HAL_GetTick());
			start_pending_fades();
			LED_refresh();
			res_init(0,AckStatus_Ok,AckReport_Ok);
			break;
		case SetEEPRom:
			if (req.Set_adress < sizeof(dummyEEPRom)) {
				dummyEEPRom[req.Set_adress] = req.writeData;
				res_init(0,AckStatus_Ok,AckReport_Ok);
			} else {
				res_init(0,AckStatus_Ok,AckReport_ParamError);
			}
			break;
		case GetEEPRom:
			//GetEEPRom
			if (req.Get_adress < sizeof(dummyEEPRom)) {
				res.eepData = dummyEEPRom[req.Get_adress];
				res_init(1,AckStatus_Ok,AckReport_Ok);
			} else {
				res_init(0,AckStatus_Ok,AckReport_ParamError);
			}
			//HAL_UART_Transmit_DMA(&huart1, mai_led_eeprom_response, 9);
			break;
		case GetBoardInfo:
			  memcpy(res.boardNo, "15070-04", 8);
			  res.boardNo[8] = 0xFF;
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
	}
}

void LED_Task_ProcessPending(void)
{
    LedRxFrame frame;

    if (led_uart_rx_restart_pending != 0u) {
        if (led_uart_start_receive_to_idle() != 0u) {
            led_uart_rx_restart_pending = 0u;
        }
    }

    while (led_rx_frame_pop(&frame)) {
        LED_Task_Process(frame.data, frame.len);
    }
}
