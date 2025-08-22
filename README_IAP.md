# IAP功能实现总结

## 功能概述
为STM32G431固件添加了IAP功能，支持通过串口命令(`0x21`)重启进入USB-DFU模式。

## 修改的文件

### 1. 串口命令定义
**文件**: `Core/Inc/slider.h`
- 添加: `SERIAL_CMD_JUMP_TO_DFU = 0x21`

### 2. DFU跳转实现
**文件**: `Core/Inc/dfu_jump.h` 和 `Core/Src/dfu_jump.c`
- `Request_DFU_Mode_And_Reset()` - 设置DFU请求标志并复位
- `Check_DFU_Request()` - 检查是否有DFU请求
- `Enhanced_Jump_To_DFU_Mode()` - 跳转到DFU引导加载程序

### 3. 主程序集成
**文件**: `Core/Inc/main.h` 和 `Core/Src/main.c`
- 包含dfu_jump.h头文件
- 在main()开始时检查DFU请求
- 保留原有的`Jump_To_DFU_Mode()`函数(向后兼容)

### 4. 命令处理
**文件**: `Core/Src/app_freertos.c`
- 在`Command_Task`中添加对`SERIAL_CMD_JUMP_TO_DFU`的处理
- 发送确认消息后调用`Request_DFU_Mode_And_Reset()`

## 使用方法
1. 发送串口命令: `FF 21 00 21`
2. 设备返回确认: `FF 21 01 01 22`
3. 设备重启并进入DFU模式
4. 使用DFU工具进行固件更新

## 技术特点
- **安全可靠**: 使用魔术标志+系统复位的方式
- **向后兼容**: 不影响现有功能
- **内存节省**: 无需修改Flash布局，使用全部112KB空间
- **易于维护**: 代码简洁，逻辑清晰

## 实现原理
1. 接收DFU命令后，在RAM中设置魔术标志
2. 执行系统复位
3. 系统重启后检查魔术标志
4. 如果发现标志，清除标志并跳转到STM32内置DFU引导加载程序
5. 进入DFU模式，可通过USB进行固件更新
