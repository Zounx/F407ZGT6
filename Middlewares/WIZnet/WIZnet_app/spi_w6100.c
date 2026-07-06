/**
 * @file    spi_w6100.c
 * @brief   W6100 SPI 底层驱动（基于 bsp_spi，ioLibrary 回调）
 *
 * ============================================================================
 *  设计说明
 * ============================================================================
 *
 *  本模块提供 W6100 通过 SPI1 通信所需的底层读写函数。
 *  上层 ioLibrary（w6100.c）中的 WIZCHIP_READ / WIZCHIP_WRITE 等函数
 *  会调用本模块通过 reg_wizchip_spi_cbfunc 注册的回调。
 *
 *  引用资源：
 *    - SPI1（PA5 SCK, PA6 MISO, PA7 MOSI）
 *    - PA4 软件 CS
 *    - PC6 硬件复位
 *
 */

#include "spi_w6100.h"
#include "ETH_WIZdemo.h"
#include "bsp_spi.h"
#include "stm32f4xx_hal.h"
#include <string.h>


/* 内部 SPI 对象（用户未提供时使用此静态实例） */
static struct bsp_spi  s_spi;
static struct bsp_spi *s_p_spi = NULL;

/* RST 引脚定义 */
#define W6100_RST_PORT  GPIOC
#define W6100_RST_PIN   GPIO_PIN_6

/* ============================================================================
 * W6100 SPI 接口初始化
 * ============================================================================ */

int w6100_spi_init(struct bsp_spi *me)
{
    if (me == NULL) {
        me = &s_spi;
    }

    /* 清空结构体，确保所有字段为默认值 */
    memset(me, 0, sizeof(*me));

    /* ---- 自定义引脚 ---- */
    me->cs_port  = GPIOA;   me->cs_pin  = GPIO_PIN_4;

    /* ---- 时序：MODE0（CPOL=0, CPHA=0） ---- */
    me->cpol     = SPI_POLARITY_LOW;
    me->cpha     = SPI_PHASE_1EDGE;
    /* 使用 64 分频（约 1.3MHz），与 test_tx 一致，降低速度确保信号稳定 */
    me->baud_prescaler = SPI_BAUDRATEPRESCALER_64;

    /* ---- 初始化 SPI1 ---- */
    if (bsp_spi_init(me, SPI1) != 0) {
        ETH_DEBUG("[W6100] SPI1 init FAILED\r\n");
        return -1;
    }
//    printf("[W6100] SPI1 init OK (CR1=0x%08lx, CFG2=0x%08lx)\r\n",
//           me->handle.Instance->CR1, me->handle.Instance->CFG2);

    /* ---- 配置 RST 引脚（PC6 推挽输出，初始高电平） ---- */
    {
        GPIO_InitTypeDef gpio = {0};
        __HAL_RCC_GPIOC_CLK_ENABLE();
        gpio.Pin   = W6100_RST_PIN;
        gpio.Mode  = GPIO_MODE_OUTPUT_PP;
        gpio.Pull  = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(W6100_RST_PORT, &gpio);
        HAL_GPIO_WritePin(W6100_RST_PORT, W6100_RST_PIN, GPIO_PIN_SET);
    }

    s_p_spi = me;
    return 0;
}

/* 提供给上层模块做单事务 SPI 传输（地址+数据不带间隙） */
int w6100_hal_xfer(uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if (!s_p_spi || !tx || !rx || len == 0) return -1;
    return bsp_spi_xfer(s_p_spi, tx, rx, len);
}

/* ============================================================================
 * W6100 硬件复位
 * ============================================================================ */

void w6100_hw_reset(void)
{
    HAL_GPIO_WritePin(W6100_RST_PORT, W6100_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(W6100_RST_PORT, W6100_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(300);
}

/* ============================================================================
 * CS 回调
 * ============================================================================ */

void w6100_cs_sel(void)
{
    if (s_p_spi)
        bsp_spi_cs_set(s_p_spi, true);   /* CS 低 = 选中 */
    /* CS 建立延时（参考 test_tx: eth_cs_delay 约 200 空循环） */
    for (volatile int i = 0; i < 200; i++) {}
}

void w6100_cs_desel(void)
{
    if (s_p_spi)
        bsp_spi_cs_set(s_p_spi, false);  /* CS 高 = 释放 */
}

/* ============================================================================
 * SPI 字节 / 突发读写
 *
 * ioLibrary 的 w6100.c 在 VDM 模式下通过 _write_byte / _read_byte 等
 * 回调函数完成与 W6100 的 SPI 通信。
 *
 * 注意：STM32H7 HAL_SPI_TransmitReceive 不允许 tx/rx 为 NULL，
 * 因此 burst write/read 即使不需要接收/发送也必须提供有效缓冲区。
 * ============================================================================ */

uint8_t w6100_spi_read_byte(void)
{
    if (!s_p_spi) return 0xFF;
    /* 参考 test_tx: 读数据阶段发 0x00 占位 */
    return bsp_spi_rw(s_p_spi, 0x00);
}

void w6100_spi_write_byte(uint8_t tx)
{
    if (!s_p_spi) return;
    bsp_spi_rw(s_p_spi, tx);
}

void w6100_spi_read_burst(uint8_t *buf, int16_t len)
{
    if (!s_p_spi || buf == NULL || len == 0) return;
    /* 使用 len <= 64 的临时补丁，后续可改为动态或循环分块 */
    uint8_t dummy[64];
    memset(dummy, 0xFF, sizeof(dummy));
    if (len <= 64) {
        bsp_spi_xfer(s_p_spi, dummy, buf, len); /* 发 0xFF 占位收数据 */
    } else {
        uint16_t pos = 0;
        while (pos < len) {
            uint16_t chunk = (len - pos > 64) ? 64 : (len - pos);
            bsp_spi_xfer(s_p_spi, dummy, buf + pos, chunk);
            pos += chunk;
        }
    }
}

void w6100_spi_write_burst(uint8_t *buf, int16_t len)
{
    if (!s_p_spi || buf == NULL || len == 0) return;
    /* HAL 需要 rx 缓冲区，用临时数组丢弃接收数据 */
    uint8_t dummy[64];
    if (len <= 64) {
        bsp_spi_xfer(s_p_spi, buf, dummy, len);
    } else {
        uint16_t pos = 0;
        while (pos < len) {
            uint16_t chunk = (len - pos > 64) ? 64 : (len - pos);
            bsp_spi_xfer(s_p_spi, buf + pos, dummy, chunk);
            pos += chunk;
        }
    }
}


/* ============================================================================
 * 直接寄存器 SPI 访问（绕过 HAL，与 test_tx 的 eth_spi_rw 完全一致）
 *
 * 使用场景：诊断对比 — 如果此方式能正常写 Sn_MR，说明问题在 HAL 层；
 *          如果此方式也失败，说明问题在更底层的硬件 / 时序。
 * ============================================================================ */

uint8_t w6100_spi_direct_rw(uint8_t data)
{
    /* 等待 TX 缓冲区空 (TXE=1) */
    while (!(SPI1->SR & SPI_SR_TXE)) {}
    /* 发送数据 */
    SPI1->DR = data;
    /* 等待接收缓冲区非空 (RXNE=1) */
    while (!(SPI1->SR & SPI_SR_RXNE)) {}
    /* 返回接收到的数据 */
    return (uint8_t)SPI1->DR;
}

void w6100_cs_direct_sel(void)
{
    /* GPIOA->BSRR bit set/reset: BR4=1 (reset/低) 选中 */
    GPIOA->BSRR = (uint32_t)(GPIO_PIN_4 << 16U);
    /* CS 建立延时（与 test_tx 一致） */
    for (volatile int i = 0; i < 200; i++) {}
}

void w6100_cs_direct_desel(void)
{
    /* GPIOA->BSRR: BS4=1 (set/高) 释放 */
    GPIOA->BSRR = (uint32_t)GPIO_PIN_4;
}

