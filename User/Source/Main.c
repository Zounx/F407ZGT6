/**
  ******************************************************************************
  * @file    Main.c
  * @brief   Main program body
  ******************************************************************************
  */

#include "main.h"

/**
  * @brief  系统时钟配置
  * @note   默认使用 HSE 25MHz -> PLL -> 168MHz
  */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* 使能 HSE 振荡器，配置 PLL */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;          /* HSE 25MHz / 25 = 1MHz */
  RCC_OscInitStruct.PLL.PLLN = 336;         /* 1MHz * 336 = 336MHz */
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2; /* 336MHz / 2 = 168MHz (SYSCLK) */
  RCC_OscInitStruct.PLL.PLLQ = 7;           /* 336MHz / 7 = 48MHz (USB/SDIO) */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* 配置 SYSCLK, HCLK, PCLK1, PCLK2 */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;    /* HCLK = 168MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;     /* APB1 = 42MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;     /* APB2 = 84MHz */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  错误处理
  */
void Error_Handler(void)
{
  /* 关闭全局中断 */
  __disable_irq();
  /* 进入死循环 */
  while (1)
  {
  }
}

/**
  * @brief  主函数
  */
int main(void)
{
  /* HAL 库初始化 */
  HAL_Init();

  /* 配置系统时钟 */
  SystemClock_Config();

  /* TODO: 外设初始化 */

  /* 主循环 */
  while (1)
  {
    /* TODO: 应用程序主循环 */
  }
}
