#ifndef _LED_H
#define _LED_H

#include "stdio.h"
#include "dma.h"
#include "tim.h"
enum {
  Sync = 0xE0,
  Marker = 0xD0,

  // ResetCommand = 0x10,
  // SetTimeout = 0x11,
  SetLedGs8Bit = 0x31,
  SetLedGs8BitMulti = 0x32,
  SetLedGs8BitMultiFade = 0x33,
  SetLedFet = 0x39,
  SetDcUpdate = 0x3B,
  SetLedGsUpdate = 0x3C,
  SetDc = 0x3F,
  SetEEPRom = 0x7B,
  GetEEPRom = 0x7C,
  SetEnableResponse = 0x7D,
  SetDisableResponse = 0x7E,
  // SetLedDirect = 0x82,
  GetBoardInfo = 0xF0,
  GetBoardStatus = 0xF1,
  GetFirmSum = 0xF2,
  GetProtocolVersion = 0xF3,
  // SetBootMode = 0xFD,

  AckStatus_Ok = 0x01,
  AckStatus_SumError = 0x02,
  AckStatus_ParityError = 0x03,
  AckStatus_FramingError = 0x04,
  AckStatus_OverRunError = 0x05,
  AckStatus_RecvBfOverFlow = 0x06,
  AckStatus_Invalid = 0xFF,

  AckReport_Ok = 0x01,
  AckReport_Busy = 0x02,
  AckReport_CommandUnknown = 0x03,
  AckReport_ParamError = 0x04,
  AckReport_Invalid = 0xFF
};

typedef union {
  uint8_t bytes[39];
  struct {
    struct {
      // uint8_t sync;
      uint8_t dstNodeID;
      uint8_t srcNodeID;
      uint8_t length;
      uint8_t cmd;
    };
    union {
      uint8_t timeout;  // SetTimeout
      struct {          // SetLedGs8Bit
        uint8_t index;
        uint8_t color[3];
      };
      struct {  // SetLedGs8BitMulti, SetLedGs8BitMultiFade, SetDc
        uint8_t start;
        uint8_t end;  //length
        uint8_t skip;
        uint8_t Multi_color[3];
        uint8_t speed;  // SetDc no exist
      };
      struct {  // SetLedFet
        uint8_t BodyLed;
        uint8_t ExtLed;
        uint8_t SideLed;
      };
      struct {  // SetEEPRom
        uint8_t Set_adress;
        uint8_t writeData;
      };
      uint8_t Get_adress;           // GetEEPRom
      uint8_t Direct_color[11][3];  // SetLedDirect
    };
    // uint8_t sum;
  };
} PacketReq;

typedef union {
  uint8_t bytes[12];
  struct {
    struct {
      // uint8_t sync;
      uint8_t dstNodeID;
      uint8_t srcNodeID;
      uint8_t length;
      uint8_t status;
      uint8_t cmd;
      uint8_t report;
    };
    union {
      uint8_t eepData;  // GetEEPRom
      struct {          // GetBoardInfo
        uint8_t boardNo[9];
        uint8_t firmRevision;
      };
      struct {  // GetBoardStatus
        uint8_t timeoutStat;
        uint8_t timeoutSec;
        uint8_t pwmIo;
        uint8_t fetTimeout;
      };
      struct {  // GetFirmSumCommand
        uint8_t sum_upper;
        uint8_t sum_lower;
      };
      struct {  // GetProtocolVersionCommand
        uint8_t appliMode;
        uint8_t major;
        uint8_t minor;
      };
    };
    // uint8_t sum;
  };
} PacketRes;

extern PacketReq req;
extern PacketRes res;

extern uint8_t WS2812_data_raw[24];
extern uint8_t WS2812_data_button[24];
extern uint8_t WS2812_data_billboard[24];
extern uint16_t led_fade_time;
extern uint8_t led_fade_target[2];
extern uint8_t led_fade_flag;
extern uint8_t led_fade_color[2][3];
extern uint16_t led_fade_clock;

void FET_LED_Init();
void FET_LED_Update(uint8_t BodyLed,uint8_t ExtLed,uint8_t SideLed);
void LED_set(uint8_t led_no,uint8_t r,uint8_t g,uint8_t b);
void LED_refresh();
void LED_update_button();
void LED_UART_Init();
void LED_UART_IRQHandler();
void LED_Fade_IRQHandler();
void LED_Task_Process();
#endif
