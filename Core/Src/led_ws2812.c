#include "led_ws2812.h"
#include "tim.h"
#include "critical_section.h"
#include <string.h>

#define NUM_LED 16
#define PRE_BUTTON_LED 2
#define BUTTON_LED_COUNT (NUM_LED / PRE_BUTTON_LED)
#define WS2812_HIGH 143
#define WS2812_LOW 67
#define LED_WS2812_DMA_LENGTH (64u + NUM_LED * 24u + 64u)

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

uint8_t WS2812_data[NUM_LED * 3]; //16LED
uint16_t WS2812_data_DMA_buffer[64 + NUM_LED * 24 + 64];

static volatile uint8_t led_refresh_pending = 0u;
static volatile uint8_t led_dma_active = 0u;

void LED_set(uint8_t led_no,uint8_t r,uint8_t g,uint8_t b){
	if(led_no >= BUTTON_LED_COUNT){
		return;
	}
	for(uint8_t i = 0;i<PRE_BUTTON_LED;i++){
		WS2812_data[(led_no * 2  + i ) * 3  ] = r;
		WS2812_data[(led_no * 2  + i ) * 3 + 1] = g;
		WS2812_data[(led_no * 2  + i ) * 3 + 2] = b;
	}
}

static void led_build_dma_buffer(void)
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
}

static void led_try_start_refresh(void)
{
	HAL_StatusTypeDef status;
	uint32_t primask;

	primask = critical_section_enter();
	if ((led_refresh_pending == 0u) || (led_dma_active != 0u)) {
		critical_section_exit(primask);
		return;
	}
	led_dma_active = 1u;
	led_refresh_pending = 0u;
	critical_section_exit(primask);

	led_build_dma_buffer();
	status = HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_2,
			(uint32_t *)WS2812_data_DMA_buffer, LED_WS2812_DMA_LENGTH);
	if (status == HAL_OK) {
		return;
	}

	primask = critical_section_enter();
	led_refresh_pending = 1u;
	if (status != HAL_BUSY) {
		led_dma_active = 0u;
	}
	critical_section_exit(primask);
}

void LED_refresh(void)
{
	uint32_t primask = critical_section_enter();

	led_refresh_pending = 1u;
	critical_section_exit(primask);
}

void LED_ServiceRefresh(void)
{
	led_try_start_refresh();
}

void led_ws2812_notify_dma_complete(void)
{
	led_dma_active = 0u;
}

void led_ws2812_reset(void)
{
	memset(WS2812_data_DMA_buffer, 0, sizeof(WS2812_data_DMA_buffer));
	led_refresh_pending = 0u;
	led_dma_active = 0u;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if ((htim != NULL) && (htim->Instance == TIM3) &&
			(htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)) {
		led_ws2812_notify_dma_complete();
    }
}
