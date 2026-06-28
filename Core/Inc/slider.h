/*
 * slider.h
 *
 *  Created on: Mar 26, 2025
 *      Author: Qinh
 */

#ifndef INC_SLIDER_H_
#define INC_SLIDER_H_
#include <stdint.h>
#include "serial_protocol.h"

void slider_set_led();
void slider_scan_start();
void slider_scan_stop();
void slider_reset();
void slider_get_board_info();
void slider_scan();

extern uint8_t slider_scan_flag;

#endif /* INC_SLIDER_H_ */
