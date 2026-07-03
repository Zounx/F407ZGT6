/**
 ****************************************************************************************************
 * @file        bsp_sdio_sd.c
 * @brief       SDIO + SD 卡底层驱动实现（F407 版）
 * @note        阻塞模式，暂不使用 DMA 中断
 *              HAL_SD_MspInit 在 HAL_SD_Init 内部自动回调
 ****************************************************************************************************
 */

#include "bsp_sdio_sd.h"

/* ============================================================================
 * 全局句柄和卡信息（供 diskio.c 等模块引用）
 * ============================================================================ */
SD_HandleTypeDef          g_sd_handle;
HAL_SD_CardInfoTypeDef    g_sd_card_info;

/* ============================================================================
 * HAL MspInit — 由 HAL_SD_Init() 自动回调
 * 配置 SDIO 引脚、时钟、NVIC
 * ============================================================================ */
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef gpio_init = {0};

    /* 使能外设时钟 */
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* SDIO 数据/时钟脚：PC8-D0, PC9-D1, PC10-D2, PC11-D3, PC12-CK */
    gpio_init.Pin      = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                         GPIO_PIN_11 | GPIO_PIN_12;
    gpio_init.Mode     = GPIO_MODE_AF_PP;
    gpio_init.Pull     = GPIO_PULLUP;
    gpio_init.Speed    = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    /* SDIO 命令脚：PD2-CMD */
    gpio_init.Pin      = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio_init);
}

/* ============================================================================
 * sd_init — 初始化 SD 卡
 * ============================================================================ */
uint8_t sd_init(void)
{
    /* HAL SD 初始化参数 */
    g_sd_handle.Instance                 = SDIO;
    g_sd_handle.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
    g_sd_handle.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    g_sd_handle.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    g_sd_handle.Init.BusWide             = SDIO_BUS_WIDE_1B;
    g_sd_handle.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    g_sd_handle.Init.ClockDiv            = 2;  /* 48MHz / (2+2) = 12MHz，降低时钟提高写入稳定性 */

    if (HAL_SD_Init(&g_sd_handle) != HAL_OK)
        return 1;

    /* 获取卡信息（扇区大小、总扇区数等） */
    if (HAL_SD_GetCardInfo(&g_sd_handle, &g_sd_card_info) != HAL_OK)
        return 1;

    /* 部分 SD 卡在 4-bit 宽总线模式下写入不稳定，暂时使用 1-bit 模式 */
#if 0
    /* 切换到 4-bit 宽总线 */
    if (HAL_SD_ConfigWideBusOperation(&g_sd_handle, SDIO_BUS_WIDE_4B) != HAL_OK)
        return 1;
#endif

    return 0;
}

/* ============================================================================
 * sd_read_disk — 读扇区（阻塞模式）
 * ============================================================================ */
uint8_t sd_read_disk(uint8_t *buf, uint32_t sector, uint32_t cnt)
{
    if (HAL_SD_ReadBlocks(&g_sd_handle, buf, sector, cnt, 5000U) != HAL_OK)
        return 1;
    return 0;
}

/* ============================================================================
 * sd_write_disk — 写扇区（阻塞模式）
 * ============================================================================ */
uint8_t sd_write_disk(uint8_t *buf, uint32_t sector, uint32_t cnt)
{
    if (HAL_SD_WriteBlocks(&g_sd_handle, buf, sector, cnt, 5000U) != HAL_OK)
        return 1;
    return 0;
}
