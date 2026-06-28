#include "led_fet.h"
#include "tim.h"

void FET_LED_Init(){
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_4);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,5);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,5);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,5);
}
void FET_LED_Update(uint8_t BodyLed,uint8_t ExtLed,uint8_t SideLed){
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_1,BodyLed);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_2,ExtLed);
	__HAL_TIM_SET_COMPARE(&htim4,TIM_CHANNEL_4,SideLed);
}
