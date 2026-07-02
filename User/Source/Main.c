/**
  ******************************************************************************
  * @file    Main.c
  * @brief   Main program body
  ******************************************************************************
  */

#include "main.h"
#include "bsp_usart.h"
#include "ExtHardwareTest.h"

/** USART1 对象定义（非 static，供 stm32f4xx_it.c 使用） */
struct bsp_usart s_usart1;

/**
  * @brief  主函数
  */
int main(void)
{
  /* HAL 库初始化 */
  HAL_Init();

  /* 调度器初始化（含系统时钟配置 HSE→PLL→168MHz） */
  scheduler_init();

  /* 初始化 USART1：PA9(TX) / PA10(RX) @115200，DMA2_Stream7(TX) / DMA2_Stream2(RX) */
  bsp_usart_init(&s_usart1, USART1, GPIOA, GPIO_PIN_9, GPIOA, GPIO_PIN_10,
                 115200, DMA2_Stream7, DMA2_Stream2,
                 8, 1024, NULL);

  bsp_usart_send_str(&s_usart1, "USART1 初始化完成\r\n");

  /* 初始化外部 SRAM（FSMC Bank3，0x68000000） */
  bsp_sram_init();

  /* 初始化全部硬件定时器（TIM1~14），TIM6 为 1ms 心跳 */
  scheduler_tim_init();

  /* 主循环：调度器运行（TIM6 中断内轮询 USART + 测试命令） */
  while (1)
  {
    scheduler_run();
  }
}
