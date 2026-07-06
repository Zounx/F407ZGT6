/**
 * @file    bsp_spi.c
 * @brief   SPI 驱动实现（F4 HAL 版本）
 *
 * ============================================================================
 *  架构说明
 * ============================================================================
 *
 *  本模块封装 STM32F4 的 SPI 外设，提供 OOP 风格驱动。
 *
 *  初始化流程：
 *    1. 使能 SPI 时钟
 *    2. 使能 GPIO 时钟
 *    3. 配置 SPI 引脚为复用功能
 *    4. 调用 HAL_SPI_Init
 *
 *  传输流程（bsp_spi_rw / bsp_spi_xfer）：
 *    1. HAL_SPI_TransmitReceive 全双工传输
 *    2. 返回接收数据
 *
 *  软件 CS 管理：
 *    通过 bsp_spi_cs_set() 手动控制 CS 引脚，
 *    在传输前后由用户自行调用（或通过 ioLibrary 的 CS 回调自动调用）。
 */

#include "bsp_spi.h"
#include <string.h>


/* ============================================================================
 * 内部辅助：使能 GPIO 端口时钟
 * ============================================================================ */

static int _gpio_clk_enable(GPIO_TypeDef *port)
{
    if      (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
    else return -1;
    return 0;
}


/* ============================================================================
 * 内部辅助：配置一个 SPI 复用引脚
 * ============================================================================ */

static void _pin_af(GPIO_TypeDef *port, uint16_t pin, uint8_t af, uint32_t pull)
{
    GPIO_InitTypeDef g = {0};

    _gpio_clk_enable(port);
    g.Pin = pin;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = pull;
    g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    g.Alternate = af;
    HAL_GPIO_Init(port, &g);
}


/* ============================================================================
 * 查询 SPI 实例对应的 GPIO 复用功能号
 * ============================================================================ */

static uint8_t _spi_af(SPI_TypeDef *instance)
{
    if      (instance == SPI1) return GPIO_AF5_SPI1;
    else if (instance == SPI2) return GPIO_AF5_SPI2;
    else if (instance == SPI3) return GPIO_AF6_SPI3;
    return 0;
}


/* ============================================================================
 * 初始化 / 反初始化
 * ============================================================================ */

int bsp_spi_init(struct bsp_spi *me, SPI_TypeDef *instance)
{
    uint8_t af;

    if (!me || !instance)
        return -1;

    SPI_DEBUG("  SPI_dbg: init...\r\n");

    me->instance = instance;

    /* 使能 SPI 时钟 */
    SPI_DEBUG("  SPI_dbg: clock enable...\r\n");
    if      (instance == SPI1) { __HAL_RCC_SPI1_CLK_ENABLE(); }
    else if (instance == SPI2) { __HAL_RCC_SPI2_CLK_ENABLE(); }
    else if (instance == SPI3) { __HAL_RCC_SPI3_CLK_ENABLE(); }
    else return -3;
    SPI_DEBUG("  SPI_dbg: clock OK\r\n");

    /* 填充默认引脚（如果未自定义） */
    if (me->sck_port  == NULL) { me->sck_port  = BSP_SPI1_SCK_PORT;  me->sck_pin  = BSP_SPI1_SCK_PIN; }
    if (me->mosi_port == NULL) { me->mosi_port = BSP_SPI1_MOSI_PORT; me->mosi_pin = BSP_SPI1_MOSI_PIN; }
    if (me->miso_port == NULL) { me->miso_port = BSP_SPI1_MISO_PORT; me->miso_pin = BSP_SPI1_MISO_PIN; }

    /* 填充默认时序（仅当用户未设置时） */
    if (me->baud_prescaler == 0) me->baud_prescaler = BSP_SPI_DEFAULT_BAUD_PRESCALER;
    /* cpol/cpha 默认值 0 对应 SPI_POLARITY_LOW / SPI_PHASE_1EDGE（MODE0），
     * 由用户决定，不覆盖默认 */

    /* 配置引脚 */
    SPI_DEBUG("  SPI_dbg: GPIO config...\r\n");
    af = _spi_af(instance);
    if (af == 0) return -3;
    /* 全部上拉（参考 test_tx 工程硬件配置，SPI 引脚在空闲时不应浮空） */
    _pin_af(me->sck_port,  me->sck_pin,  af, GPIO_PULLUP);
    _pin_af(me->mosi_port, me->mosi_pin, af, GPIO_PULLUP);
    _pin_af(me->miso_port, me->miso_pin, af, GPIO_PULLUP);

    /* 配置 CS（如果指定） */
    if (me->cs_port) {
        GPIO_InitTypeDef g = {0};
        _gpio_clk_enable(me->cs_port);
        g.Pin = me->cs_pin;
        g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(me->cs_port, &g);
        HAL_GPIO_WritePin(me->cs_port, me->cs_pin, GPIO_PIN_SET);
    }
    SPI_DEBUG("  SPI_dbg: GPIO OK\r\n");

    /* HAL SPI Init */
    SPI_DEBUG("  SPI_dbg: HAL_SPI_Init...\r\n");
    me->handle.Instance = instance;
    me->handle.Init.Mode           = SPI_MODE_MASTER;
    me->handle.Init.Direction      = SPI_DIRECTION_2LINES;
    me->handle.Init.DataSize       = SPI_DATASIZE_8BIT;
    me->handle.Init.CLKPolarity    = me->cpol;
    me->handle.Init.CLKPhase       = me->cpha;
    me->handle.Init.NSS            = SPI_NSS_SOFT;
    me->handle.Init.BaudRatePrescaler = me->baud_prescaler;
    me->handle.Init.FirstBit       = SPI_FIRSTBIT_MSB;
    me->handle.Init.TIMode         = SPI_TIMODE_DISABLE;
    me->handle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    me->handle.Init.CRCPolynomial  = 0;

    if (HAL_SPI_Init(&me->handle) != HAL_OK) {
        SPI_DEBUG("  SPI_dbg: HAL_SPI_Init FAILED\r\n");
        return -2;
    }

    SPI_DEBUG("  SPI_dbg: CR1=0x%08lx, CR2=0x%08lx\r\n",
           me->handle.Instance->CR1,
           me->handle.Instance->CR2);

    SPI_DEBUG("  SPI_dbg: HAL_SPI_Init OK\r\n");

    me->initialized = true;
    SPI_DEBUG("  SPI_dbg: init complete\r\n");
    return 0;
}


int bsp_spi_deinit(struct bsp_spi *me)
{
    if (!me || !me->initialized)
        return -1;

    HAL_SPI_DeInit(&me->handle);

    HAL_GPIO_DeInit(me->sck_port, me->sck_pin);
    HAL_GPIO_DeInit(me->mosi_port, me->mosi_pin);
    HAL_GPIO_DeInit(me->miso_port, me->miso_pin);
    if (me->cs_port)
        HAL_GPIO_DeInit(me->cs_port, me->cs_pin);

    if      (me->instance == SPI1) __HAL_RCC_SPI1_CLK_DISABLE();
    else if (me->instance == SPI2) __HAL_RCC_SPI2_CLK_DISABLE();
    else if (me->instance == SPI3) __HAL_RCC_SPI3_CLK_DISABLE();

    me->initialized = false;
    return 0;
}


/* ============================================================================
 * 数据收发
 * ============================================================================ */

uint8_t bsp_spi_rw(struct bsp_spi *me, uint8_t tx)
{
    uint8_t rx = 0;

    if (!me || !me->initialized)
        return 0xFF;

    HAL_SPI_TransmitReceive(&me->handle, &tx, &rx, 1, 100);
    return rx;
}


int bsp_spi_xfer(struct bsp_spi *me, uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if (!me || !me->initialized)
        return -1;

    if (HAL_SPI_TransmitReceive(&me->handle, tx, rx, len, 1000) != HAL_OK)
        return -2;

    return 0;
}


/* ============================================================================
 * CS 控制
 * ============================================================================ */

void bsp_spi_cs_set(struct bsp_spi *me, bool assert)
{
    if (me && me->cs_port)
        HAL_GPIO_WritePin(me->cs_port, me->cs_pin,
            assert ? GPIO_PIN_RESET : GPIO_PIN_SET);
}


/* ============================================================================
 * 硬件自检
 * ============================================================================ */

int bsp_spi_diag(struct bsp_spi *me)
{
    SPI_TypeDef *s;
    uint32_t cr1, cr2, sr;
    (void)cr2;

    if (!me || !me->initialized) {
        SPI_DEBUG("\n========== SPI HARDWARE DIAG [FAIL] ==========\n");
        SPI_DEBUG("  SPI is NOT initialized!\n");
        SPI_DEBUG("==============================================\n\n");
        return -1;
    }

    s = me->instance;
    cr1 = s->CR1;
    cr2 = s->CR2;
    sr  = s->SR;

    SPI_DEBUG("\n========== SPI HARDWARE DIAG [START] ==========\n");
    SPI_DEBUG("  SPI base: 0x%p\n", (void *)s);
    SPI_DEBUG("\n");

    /* ---- CR1 ---- */
    SPI_DEBUG("  [CR1] Control Reg 1 = 0x%08lx\n", cr1);
    SPI_DEBUG("    SPE  (periph enable) = %lu  %s\n",
           (cr1 >> 6) & 1,
           ((cr1 >> 6) & 1) ? "[OK]" : "[FAIL] SPI disabled!");
    SPI_DEBUG("    MSTR (master)        = %lu  %s\n",
           (cr1 >> 2) & 1,
           ((cr1 >> 2) & 1) ? "[OK]" : "[FAIL] not master!");
    SPI_DEBUG("    CPOL = %lu  CPHA = %lu\n",
           (cr1 >> 1) & 1, cr1 & 1);
    SPI_DEBUG("    BR   = %lu (prescaler)\n", (cr1 >> 3) & 0x07);
    SPI_DEBUG("\n");

    /* ---- CR2 ---- */
    SPI_DEBUG("  [CR2] Control Reg 2 = 0x%08lx\n", cr2);
    SPI_DEBUG("\n");

    /* ---- SR ---- */
    SPI_DEBUG("  [SR]  Status Reg    = 0x%08lx\n", sr);
    SPI_DEBUG("    RXNE (rx not empty) = %lu\n", (sr >> 0) & 1);
    SPI_DEBUG("    TXE  (tx empty)     = %lu\n", (sr >> 1) & 1);
    SPI_DEBUG("    BSY  (busy)         = %lu  %s\n",
           (sr >> 7) & 1,
           ((sr >> 7) & 1) ? "[BUSY]" : "[OK] idle");
    SPI_DEBUG("\n");

    SPI_DEBUG("  Diagnosis:\n");
    if ((cr1 >> 2) & 1)
        SPI_DEBUG("    [OK]   SPI master mode\n");
    if ((cr1 >> 6) & 1)
        SPI_DEBUG("    [OK]   SPI enabled\n");
    if (!((sr >> 7) & 1))
        SPI_DEBUG("    [OK]   SPI idle (not busy)\n");
    SPI_DEBUG("===============================================\n\n");

    return 0;
}
