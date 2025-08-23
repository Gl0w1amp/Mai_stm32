/**
  ******************************************************************************
  * @file    dfu_jump.h
  * @brief   DFU jump implementation header
  ******************************************************************************
  */

#ifndef __DFU_JUMP_H
#define __DFU_JUMP_H

#include <stdint.h>

/* Function prototypes */
void Request_DFU_Mode_And_Reset(void);
uint8_t Check_DFU_Request(void);
void Enhanced_Jump_To_DFU_Mode(void);

#endif /* __DFU_JUMP_H */
