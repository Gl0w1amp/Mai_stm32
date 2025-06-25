/*
 * button.h
 *
 *  Created on: Aug 1, 2024
 *      Author: Qinh
 */

#ifndef INC_BUTTON_H_
#define INC_BUTTON_H_

#include "main.h"

extern uint8_t button[2];
typedef union{
  uint8_t data[8];
  struct{
    float num;
    uint8_t sync[4];
  };
}vofa_test;


void button_init();
void button_scan();
#endif /* INC_BUTTON_H_ */
