/**
 * @file    bsp_spi.h
 * @brief   SPI 驱动模块（OOP 风格，支持软件 CS）
 *
 * ============================================================================
 *  快速开始
 * ============================================================================
 *
 * // 1. 定义 SPI 对象（全局或 static）
 * static struct bsp_spi  s_spi;
 *
 * // 2. 初始化（SPI1，模式 0，2.625MHz）
 * s_spi.baud_prescaler = SPI_BAUDRATEPRESCALER_32;
 * bsp_spi_init(&s_spi, SPI1);
 *
 * // 3. 收发
 * uint8_t rx = bsp_spi_rw(&s_spi, 0xA5);
 *
 * // 4. 片选控制
 * bsp_spi_cs_set(&s_spi, true);    // CS 低
 * bsp_spi_rw(&s_spi, 0x80);        // 发指令
 * bsp_spi_rw(&s_spi, 0x00);        // 发地址
 * uint8_t dat = bsp_spi_rw(&s_spi, 0xFF); // 读数据
 * bsp_spi_cs_set(&s_spi, false);   // CS 高
 *
 * ============================================================================
 *  默认引脚（SPI1 - PA5/PA6/PA7）
 * ============================================================================
 *
 *   SCK  - PA5  (AF5)
 *   MISO - PA6  (AF5)
 *   MOSI - PA7  (AF5)
 *   CS   - （自定义，软件 GPIO）
 *
 *   如需自定义引脚，初始化前修改 me->sck_port / mosi_port / miso_port 字段。
 *
 * ============================================================================
 *  时钟说明
 * ============================================================================
 *
 *   SPI 时钟 = PCLK / 分频
 *   SPI1 挂在 APB2（84MHz）：
 *     prescaler=32 => 84/32 = 2.625MHz
 *   SPI2/3 挂在 APB1（42MHz）：
 *     prescaler=16 => 42/16 = 2.625MHz
 */

#ifndef BSP_SPI_H
#define BSP_SPI_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>


/* ============================================================================
 * 调试开关
 *
 * BSP_SPI_DEBUG 设为 1 时 SPI_DEBUG() 会通过 printf 打印调试信息；
 * 设为 0 时所有 SPI_DEBUG 被编译为空，不产生任何代码。
 *
 * 用法：
 *   #define BSP_SPI_DEBUG  1    // 开启调试
 *   #define BSP_SPI_DEBUG  0    // 关闭调试
 * ============================================================================ */

#define BSP_SPI_DEBUG  0       /* 调试开关：1=开启，0=关闭 */

#if BSP_SPI_DEBUG
    #include <stdio.h>
    #define SPI_DEBUG(...)  printf(__VA_ARGS__)
#else
    #define SPI_DEBUG(...)  ((void)0)
#endif


/* ============================================================================
 * 默认引脚配置（SPI1：PA5=SCK, PA6=MISO, PA7=MOSI）
 * ============================================================================ */

#define BSP_SPI1_SCK_PORT   GPIOA
#define BSP_SPI1_SCK_PIN    GPIO_PIN_5
#define BSP_SPI1_SCK_AF     GPIO_AF5_SPI1

#define BSP_SPI1_MOSI_PORT  GPIOA
#define BSP_SPI1_MOSI_PIN   GPIO_PIN_7
#define BSP_SPI1_MOSI_AF    GPIO_AF5_SPI1

#define BSP_SPI1_MISO_PORT  GPIOA
#define BSP_SPI1_MISO_PIN   GPIO_PIN_6
#define BSP_SPI1_MISO_AF    GPIO_AF5_SPI1


/* ============================================================================
 * 默认时序参数（模式 0，适用于 W6100 等 SPI 设备）
 * ============================================================================ */

#define BSP_SPI_DEFAULT_BAUD_PRESCALER  SPI_BAUDRATEPRESCALER_32
#define BSP_SPI_DEFAULT_CPOL           SPI_POLARITY_LOW
#define BSP_SPI_DEFAULT_CPHA           SPI_PHASE_1EDGE


/* ============================================================================
 * SPI 对象
 *
 * 每个 SPI 实例定义一个此结构体变量。
 * 所有操作通过函数完成，不要直接读写字段。
 *
 * 如需自定义引脚，在调用 init 前修改对应字段：
 *   static struct bsp_spi s_spi;
 *   s_spi.sck_port  = GPIOB;  s_spi.sck_pin  = GPIO_PIN_3;
 *   s_spi.mosi_port = GPIOB;  s_spi.mosi_pin = GPIO_PIN_5;
 *   s_spi.miso_port = GPIOB;  s_spi.miso_pin = GPIO_PIN_4;
 *   bsp_spi_init(&s_spi, SPI1);
 * ============================================================================ */

struct bsp_spi {
    SPI_TypeDef           *instance;        /* SPI1 / SPI2 / SPI3 */

    /* -------- 引脚（init 前可自定义） -------- */
    GPIO_TypeDef          *sck_port;   uint16_t sck_pin;
    GPIO_TypeDef          *mosi_port;  uint16_t mosi_pin;
    GPIO_TypeDef          *miso_port;  uint16_t miso_pin;
    GPIO_TypeDef          *cs_port;    uint16_t cs_pin;   /* 0 表示不用硬件 CS */

    /* -------- 时序参数（init 前可自定义） -------- */
    uint32_t               baud_prescaler;
    uint32_t               cpol;       /* SPI_POLARITY_LOW / HIGH */
    uint32_t               cpha;       /* SPI_PHASE_1EDGE / 2EDGE */

    /* -------- HAL 句柄 -------- */
    SPI_HandleTypeDef      handle;

    /* -------- 状态 -------- */
    bool                   initialized;
};


/* ============================================================================
 * API 函数
 * ============================================================================ */

/**
 * @brief  初始化 SPI 外设
 *
 * 完成以下工作：
 *   1. 使能 SPI 时钟和 GPIO 时钟
 *   2. 配置 SCK/MOSI/MISO 引脚为复用功能
 *   3. 如果有 cs_port，配置 CS 引脚为推挽输出
 *   4. 调用 HAL_SPI_Init
 *
 * @param me        SPI 对象指针
 * @param instance  SPI1 / SPI2 / SPI3
 *
 * @return 0  成功
 * @return -1 me / instance 为 NULL
 * @return -2 HAL_SPI_Init 失败
 * @return -3 不支持的 SPI 实例
 */
int  bsp_spi_init(struct bsp_spi *me, SPI_TypeDef *instance);

/**
 * @brief  反初始化 SPI
 *
 * 停止 SPI、关闭时钟、释放 GPIO。
 *
 * @param me  SPI 对象指针
 * @return 0  成功
 * @return -1 me 为 NULL 或未 init
 */
int  bsp_spi_deinit(struct bsp_spi *me);

/**
 * @brief  单字节收发（全双工）
 *
 * 发送一个字节同时接收一个字节。
 *
 * @param me  SPI 对象指针
 * @param tx  待发送数据
 * @return 接收到的数据
 */
uint8_t bsp_spi_rw(struct bsp_spi *me, uint8_t tx);

/**
 * @brief  多字节收发（全双工）
 *
 * 发送 tx 缓冲区的数据，同时接收数据到 rx 缓冲区。
 * tx 或 rx 可置 NULL（仅发送或仅接收）。
 *
 * @param me   SPI 对象指针
 * @param tx   发送缓冲区（NULL = 发全 0xFF 占位）
 * @param rx   接收缓冲区（NULL = 丢弃接收数据）
 * @param len  字节数
 * @return 0  成功
 * @return -1 me 为 NULL 或未 init
 * @return -2 HAL_SPI_TransmitReceive 失败
 */
int  bsp_spi_xfer(struct bsp_spi *me, uint8_t *tx, uint8_t *rx, uint16_t len);

/**
 * @brief  CS 引脚控制
 *
 * @param me     SPI 对象指针
 * @param assert true=低（选中），false=高（释放）
 *
 * @note 如果 me->cs_port 为 0，此函数什么都不做。
 */
void bsp_spi_cs_set(struct bsp_spi *me, bool assert);

/**
 * @brief  SPI 硬件自检
 *
 * 打印 CR1/CR2/SR 寄存器信息。
 *
 * @param me  SPI 对象指针
 * @return 0  成功
 * @return -1 me 为 NULL 或未 init
 *
 * @note 依赖 printf 重定向到串口
 */
int  bsp_spi_diag(struct bsp_spi *me);


#endif /* BSP_SPI_H */
