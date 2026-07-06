/**
  ******************************************************************************
  * @file    Main.c
  * @brief   Main program body
  ******************************************************************************
  */

#include "main.h"

/** USART1 对象定义（非 static，供 stm32f4xx_it.c 使用） */
struct bsp_usart s_usart1;

/**
  * @brief  主函数
  */
int main(void)
{
	int ret;
  /* HAL 库初始化 */
  HAL_Init();

  /* NVIC 优先级分组：2 位抢占 + 2 位子优先级
   * 必须 GROUP_2 才能让 SDIO(0) 优先级高于 DMA(2)，SDIO 工程即如此配置 */
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_2);

  /* 调度器初始化（含系统时钟配置 HSE->PLL->168MHz） */
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

  /* 挂载 FATFS（静默执行，测试请通过串口指令） */
  sd_mount();

/* WIZnet 以太网初始化（QSPI或SPI 硬件协议栈） */
    ret = ETH_WIZdemo_init();
    if (ret != 0) {
        bsp_usart_printf(&s_usart1, "WIZnet init failed! ret=%d\r\n", ret);
        while (1);
    }
    /* WIZnet 通信应用初始化 */
    ret = WIZnet_TcpSever_init();
    if (ret != 0)
        bsp_usart_printf(&s_usart1, "WIZ_TcpSever init failed!\r\n");
    ret = WIZnet_TcpClient_init();
    if (ret != 0)
        bsp_usart_printf(&s_usart1, "WIZ_TcpClient init failed!\r\n");
    ret = WIZnet_UDP_init();
    if (ret != 0)
        bsp_usart_printf(&s_usart1, "WIZ_UDP init failed!\r\n");
  /* 主循环：调度器运行（TIM6 中断内轮询 USART + 测试命令）+ ETH 链路监测 */
  while (1)
  {
    scheduler_run();
  }
}
