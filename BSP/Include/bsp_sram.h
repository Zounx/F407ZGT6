/**
 * @file    bsp_sram.h
 * @brief   外部 SRAM 驱动模块（IS61WV102416BLL-10TL @ FSMC Bank3）
 *
 * FSMC Bank1_NORSRAM3 (NE3) → 地址 0x68000000
 * 16 位数据总线, 1M × 16 bit = 2 MB
 */

#ifndef BSP_SRAM_H
#define BSP_SRAM_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>

/* ============================================================================
 * 地址 / 容量常量
 * ============================================================================ */

#define EXT_SRAM_ADDR       ((uint32_t)0x68000000)   /**< FSMC Bank3 基地址 */
#define EXT_SRAM_SIZE       (2UL * 1024 * 1024)      /**< 2 MB */

/* ============================================================================
 * 基本读写 API
 * ============================================================================ */

/** @brief 初始化外部 SRAM（FSMC GPIO + 控制器） */
void     bsp_sram_init(void);

/** @brief 8 位写 */
void     bsp_sram_write8(uint32_t addr, uint8_t data);

/** @brief 8 位读 */
uint8_t  bsp_sram_read8(uint32_t addr);

/** @brief 16 位写 */
void     bsp_sram_write16(uint32_t addr, uint16_t data);

/** @brief 16 位读 */
uint16_t bsp_sram_read16(uint32_t addr);

/** @brief 32 位写 */
void     bsp_sram_write32(uint32_t addr, uint32_t data);

/** @brief 32 位读 */
uint32_t bsp_sram_read32(uint32_t addr);

/* ============================================================================
 * 块操作 API
 * ============================================================================ */

/** @brief 写缓冲区到 SRAM（字节单位） */
void bsp_sram_write_buf(uint32_t addr, const uint8_t *buf, uint32_t count);

/** @brief 从 SRAM 读缓冲区（字节单位） */
void bsp_sram_read_buf(uint32_t addr, uint8_t *buf, uint32_t count);

/** @brief 填充：将 SRAM 从 addr 开始的 length 字节设为 data */
void bsp_sram_memset(uint32_t addr, uint8_t data, uint32_t length);

/** @brief SRAM 内部拷贝（dst/src 均为相对于 EXT_SRAM_ADDR 的偏移） */
void bsp_sram_memcpy(uint32_t dst, uint32_t src, uint32_t length);

/** @brief 清空整个 SRAM（写 0x00） */
void bsp_sram_clean(void);

/* ============================================================================
 * 存在性探针
 * ============================================================================ */

/**
 * @brief  SRAM 存在性探针
 * @retval 0  SRAM 正常响应
 * @retval 1  SRAM 未检测到
 * @retval 2  SRAM 数据不一致
 */
int bsp_sram_probe(void);

/* ============================================================================
 * 测试函数
 * ============================================================================ */

/**
 * @brief  March C- 详细全扫描（整个 SRAM，含 NBL 逐字节测试）
 * @note   通过 SCI 输出详细结果，无返回值
 */
void bsp_sram_scan(void);

#endif /* BSP_SRAM_H */
