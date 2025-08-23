/**
  ******************************************************************************
  * @file    dfu_jump.c
  * @brief   DFU jump implementation for STM32G431
  ******************************************************************************
  */

#include "main.h"

/* Magic pattern stored in RAM to indicate DFU request */
#define DFU_REQUEST_SIGNATURE   0xDEADBEEF
#define DFU_REQUEST_ADDRESS     (0x20000000 + 0x7F00)  // Near end of RAM

/**
  * @brief  Set DFU request flag and perform system reset
  * @retval None
  */
void Request_DFU_Mode_And_Reset(void)
{
    /* Store magic signature in RAM */
    *(volatile uint32_t*)DFU_REQUEST_ADDRESS = DFU_REQUEST_SIGNATURE;
    
    /* Ensure the write is completed */
    __DSB();
    __ISB();
    
    /* Perform system reset */
    NVIC_SystemReset();
}

/**
  * @brief  Check if DFU mode was requested before reset
  * @retval 1 if DFU requested, 0 otherwise
  */
uint8_t Check_DFU_Request(void)
{
    if (*(volatile uint32_t*)DFU_REQUEST_ADDRESS == DFU_REQUEST_SIGNATURE)
    {
        /* Clear the signature */
        *(volatile uint32_t*)DFU_REQUEST_ADDRESS = 0;
        return 1;
    }
    return 0;
}

/**
  * @brief  Jump to DFU mode (STM32 built-in USB DFU bootloader)
  * @retval None
  */
void Enhanced_Jump_To_DFU_Mode(void)
{
    uint32_t i;
    void (*SysMemBootJump)(void);
    
    /* Disable all interrupts */
    __disable_irq();
    
    /* Disable SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    
    /* Clear all interrupts */
    for (i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    
    /* Reset clock configuration */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));
    
    RCC->CFGR = 0x00000000;
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON);
    RCC->PLLCFGR = 0x00001000;
    
    /* Reset all peripherals */
    RCC->AHB1RSTR = 0xFFFFFFFF;
    RCC->AHB2RSTR = 0xFFFFFFFF;
    RCC->APB1RSTR1 = 0xFFFFFFFF;
    RCC->APB1RSTR2 = 0xFFFFFFFF;
    RCC->APB2RSTR = 0xFFFFFFFF;
    
    /* Release reset */
    RCC->AHB1RSTR = 0;
    RCC->AHB2RSTR = 0;
    RCC->APB1RSTR1 = 0;
    RCC->APB1RSTR2 = 0;
    RCC->APB2RSTR = 0;
    
    /* Enable SYSCFG clock */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    
    /* Small delay for clock stabilization */
    for (i = 0; i < 1000; i++);
    
    /* Map system memory to 0x00000000 */
    SYSCFG->MEMRMP = 0x01;
    
    /* Small delay after remap */
    for (i = 0; i < 1000; i++);
    
    /* Get bootloader addresses */
    uint32_t bootloader_sp = *(volatile uint32_t*)(0x1FFF0000);
    uint32_t bootloader_pc = *(volatile uint32_t*)(0x1FFF0004);
    
    /* Set stack pointer */
    __set_MSP(bootloader_sp);
    
    /* Jump to bootloader */
    SysMemBootJump = (void (*)(void))bootloader_pc;
    SysMemBootJump();
    
    /* Should never reach here */
    while(1);
}
