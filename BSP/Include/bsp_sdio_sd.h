/**
 ****************************************************************************************************
 * @file        bsp_sdio_sd.h
 * @brief       SDIO + SD 卡底层驱动（F407 版，使用 HAL SD 库阻塞模式）
 * @note        PC8-D0, PC9-D1, PC10-D2, PC11-D3, PC12-SCK, PD2-CMD
 ****************************************************************************************************
 */

#ifndef BSP_SDIO_SD_H
#define BSP_SDIO_SD_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* SD 卡句柄和卡信息（供 diskio.c / 其他模块引用） */
extern SD_HandleTypeDef          g_sd_handle;
extern HAL_SD_CardInfoTypeDef    g_sd_card_info;

/**
 * @brief  初始化 SD 卡（含 GPIO/MspInit, 卡识别, 4bit 宽总线切换）
 * @return 0=成功, 1=失败
 */
uint8_t sd_init(void);

/**
 * @brief  读扇区（阻塞模式）
 * @param  buf    数据缓冲区
 * @param  sector 起始扇区（LBA）
 * @param  cnt    扇区数
 * @return 0=成功, 1=失败
 */
uint8_t sd_read_disk(uint8_t *buf, uint32_t sector, uint32_t cnt);

/**
 * @brief  写扇区（阻塞模式）
 * @param  buf    数据缓冲区
 * @param  sector 起始扇区（LBA）
 * @param  cnt    扇区数
 * @return 0=成功, 1=失败
 */
uint8_t sd_write_disk(uint8_t *buf, uint32_t sector, uint32_t cnt);

#endif /* BSP_SDIO_SD_H */
