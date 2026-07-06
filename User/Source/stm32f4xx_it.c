/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
#include "bsp_usart.h"
#include "bsp_sdio_sd.h"

/******************************************************************************/
/*           Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  */
void HardFault_Handler(void)
{
  /* 进入死循环 */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Memory Manage exception.
  */
void MemManage_Handler(void)
{
  /* 进入死循环 */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Bus Fault exception.
  */
void BusFault_Handler(void)
{
  /* 进入死循环 */
  while (1)
  {
  }
}

/**
  * @brief  This function handles Usage Fault exception.
  */
void UsageFault_Handler(void)
{
  /* 进入死循环 */
  while (1)
  {
  }
}

/**
  * @brief  This function handles SVCall exception.
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s).                */
/******************************************************************************/

/**
 * @brief  USART1 中断处理
 * @note   在 stm32f4xx_it.c 或 main.c 中定义 USART1 对象：
 *           extern struct bsp_usart  s_usart1;
 *         然后在 USART1_IRQHandler 中调用：
 *           bsp_usart_irq_handler(&s_usart1);
 */
void USART1_IRQHandler(void)
{
    extern struct bsp_usart s_usart1;
    bsp_usart_irq_handler(&s_usart1);
}

/**
 * @brief  USART2 中断处理
 */
void USART2_IRQHandler(void)
{
    /* TODO: bsp_usart_irq_handler(&s_usart2); */
}

/**
 * @brief  USART3 中断处理
 */
void USART3_IRQHandler(void)
{
    /* TODO: bsp_usart_irq_handler(&s_usart3); */
}

/**
 * @brief  UART4 中断处理
 */
void UART4_IRQHandler(void)
{
    /* TODO: bsp_usart_irq_handler(&s_u4); */
}

/**
 * @brief  SDIO 中断处理（Step2：调 HAL_SD_IRQHandler，仍 PIO 读写）
 */
void SDIO_IRQHandler(void)
{
    HAL_SD_IRQHandler(&g_sd_handle);
}

/**
 * @brief  DMA2 Stream3 中断处理（SDIO RX — HAL SD DMA 模式必需）
 */
void DMA2_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_sd_hdma_rx);
}

/**
 * @brief  DMA2 Stream6 中断处理（SDIO TX — HAL SD DMA 模式必需）
 */
void DMA2_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_sd_hdma_tx);
}
