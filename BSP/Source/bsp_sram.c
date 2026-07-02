/**
 * @file    bsp_sram.c
 * @brief   外部 SRAM 驱动实现（IS61WV102416BLL-10TL @ FSMC Bank3）
 *
 * FSMC Bank1_NORSRAM3 (NE3) → 地址 0x68000000
 * 16 位数据总线, 1M × 16 bit = 2 MB
 *
 * 引脚映射（与 C:\\Users\\ZouJunxiang\\Desktop\\test 工程完全一致）:
 *   PD0/D2,  PD1/D3,  PD4/NOE, PD5/NWE,
 *   PD8/D13, PD9/D14, PD10/D15,
 *   PD11/A16, PD12/A17, PD13/A18,
 *   PD14/D0, PD15/D1
 *
 *   PE0/NBL0, PE1/NBL1, PE3/A19,
 *   PE4/A20, PE5/A21,
 *   PE7/D4~PE15/D12
 *
 *   PF0/A0~PF5/A5, PF12/A6~PF15/A9
 *
 *   PG0/A10~PG5/A15, PG10/NE3(片选)
 */

#include "bsp_sram.h"
#include "bsp_usart.h"
#include "stm32f4xx_ll_fsmc.h"

/* ============================================================================
 * 地址转换宏
 * ============================================================================ */
#define SRAM_PTR(offset)    ((volatile uint8_t  *)(EXT_SRAM_ADDR + (offset)))
#define SRAM_PTR16(offset)  ((volatile uint16_t *)(EXT_SRAM_ADDR + (offset)))
#define SRAM_PTR32(offset)  ((volatile uint32_t *)(EXT_SRAM_ADDR + (offset)))

/* USART1 对象（供 bsp_sram_scan 输出用） */
extern struct bsp_usart s_usart1;

/* 内部函数声明 */
static void bsp_sram_gpio_init(void);
static void bsp_sram_fsmc_init(void);

/* ============================================================================
 * GPIO 辅助操作（直接寄存器，与 test 工程一致）
 * ============================================================================ */

/**
 * @brief  设置 GPIO 复用功能
 * @param  GPIOx  GPIO 端口基址
 * @param  BITx   引脚号（0~15）
 * @param  AFx    复用功能编号（FSMC = AF12 = 0x0C）
 */
static inline void gpio_af_set(GPIO_TypeDef *GPIOx, uint8_t BITx, uint8_t AFx)
{
    GPIOx->AFR[BITx >> 3] &= ~(0x0FUL << ((BITx & 0x07) * 4));
    GPIOx->AFR[BITx >> 3] |= (uint32_t)AFx << ((BITx & 0x07) * 4);
}

/**
 * @brief  批量配置 GPIO 模式/速度/上下拉
 * @param  GPIOx  GPIO 端口基址
 * @param  BITx   引脚位掩码
 * @param  MODE   模式（0=输入,1=输出,2=AF,3=模拟）
 * @param  OTYPE  输出类型（0=推挽,1=开漏）
 * @param  OSPEED 速度（0~3）
 * @param  PUPD   上下拉（0=无,1=上拉,2=下拉）
 */
static inline void gpio_set(GPIO_TypeDef *GPIOx, uint32_t BITx,
                            uint32_t MODE, uint32_t OTYPE,
                            uint32_t OSPEED, uint32_t PUPD)
{
    for (uint32_t pinpos = 0; pinpos < 16; pinpos++)
    {
        uint32_t pos = 1UL << pinpos;
        if ((BITx & pos) == 0) continue;

        GPIOx->MODER  &= ~(3UL << (pinpos * 2));
        GPIOx->MODER  |=  MODE << (pinpos * 2);

        if ((MODE == 1) || (MODE == 2)) /* 输出或复用 */
        {
            GPIOx->OSPEEDR &= ~(3UL << (pinpos * 2));
            GPIOx->OSPEEDR |=  OSPEED << (pinpos * 2);
            GPIOx->OTYPER  &= ~(1UL << pinpos);
            GPIOx->OTYPER  |=  OTYPE << pinpos;
        }

        GPIOx->PUPDR &= ~(3UL << (pinpos * 2));
        GPIOx->PUPDR |=  PUPD << (pinpos * 2);
    }
}

#define FSMC_AF        ((uint8_t)0x0C)   /* AF12 = FSMC */

/* ============================================================================
 * GPIO 初始化（FSMC 复用功能 AF12）
 * ============================================================================ */
static void bsp_sram_gpio_init(void)
{
    /* 使能 GPIO 时钟 */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* === GPIOD ===
     * PD0/D2,  PD1/D3,  PD4/NOE, PD5/NWE,
     * PD8/D13, PD9/D14, PD10/D15,
     * PD11/A16, PD12/A17, PD13/A18,
     * PD14/D0, PD15/D1
     */
    gpio_af_set(GPIOD, 0,  FSMC_AF);
    gpio_af_set(GPIOD, 1,  FSMC_AF);
    gpio_af_set(GPIOD, 4,  FSMC_AF);
    gpio_af_set(GPIOD, 5,  FSMC_AF);
    gpio_af_set(GPIOD, 8,  FSMC_AF);
    gpio_af_set(GPIOD, 9,  FSMC_AF);
    gpio_af_set(GPIOD, 10, FSMC_AF);
    gpio_af_set(GPIOD, 11, FSMC_AF);
    gpio_af_set(GPIOD, 12, FSMC_AF);
    gpio_af_set(GPIOD, 13, FSMC_AF);
    gpio_af_set(GPIOD, 14, FSMC_AF);
    gpio_af_set(GPIOD, 15, FSMC_AF);

    gpio_set(GPIOD,
             (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5) |
             (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) |
             (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15),
             2, 0, GPIO_SPEED_FREQ_VERY_HIGH, 0);   /* AF, PP, 100MHz, 无上下拉 */

    /* === GPIOE ===
     * PE0/NBL0, PE1/NBL1, PE3/A19,
     * PE4/A20, PE5/A21,
     * PE7/D4~PE15/D12
     */
    gpio_af_set(GPIOE, 0,  FSMC_AF);
    gpio_af_set(GPIOE, 1,  FSMC_AF);
    gpio_af_set(GPIOE, 3,  FSMC_AF);
    gpio_af_set(GPIOE, 4,  FSMC_AF);
    gpio_af_set(GPIOE, 5,  FSMC_AF);
    gpio_af_set(GPIOE, 7,  FSMC_AF);
    gpio_af_set(GPIOE, 8,  FSMC_AF);
    gpio_af_set(GPIOE, 9,  FSMC_AF);
    gpio_af_set(GPIOE, 10, FSMC_AF);
    gpio_af_set(GPIOE, 11, FSMC_AF);
    gpio_af_set(GPIOE, 12, FSMC_AF);
    gpio_af_set(GPIOE, 13, FSMC_AF);
    gpio_af_set(GPIOE, 14, FSMC_AF);
    gpio_af_set(GPIOE, 15, FSMC_AF);

    gpio_set(GPIOE,
             (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5) |
             (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) |
             (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15),
             2, 0, GPIO_SPEED_FREQ_VERY_HIGH, 0);

    /* === GPIOF ===
     * PF0/A0~PF5/A5, PF12/A6~PF15/A9
     */
    gpio_af_set(GPIOF, 0,  FSMC_AF);
    gpio_af_set(GPIOF, 1,  FSMC_AF);
    gpio_af_set(GPIOF, 2,  FSMC_AF);
    gpio_af_set(GPIOF, 3,  FSMC_AF);
    gpio_af_set(GPIOF, 4,  FSMC_AF);
    gpio_af_set(GPIOF, 5,  FSMC_AF);
    gpio_af_set(GPIOF, 12, FSMC_AF);
    gpio_af_set(GPIOF, 13, FSMC_AF);
    gpio_af_set(GPIOF, 14, FSMC_AF);
    gpio_af_set(GPIOF, 15, FSMC_AF);

    gpio_set(GPIOF,
             (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) |
             (1 << 4) | (1 << 5) |
             (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15),
             2, 0, GPIO_SPEED_FREQ_VERY_HIGH, 0);

    /* === GPIOG ===
     * PG0/A10~PG5/A15, PG10/NE3(片选)
     */
    gpio_af_set(GPIOG, 0,  FSMC_AF);
    gpio_af_set(GPIOG, 1,  FSMC_AF);
    gpio_af_set(GPIOG, 2,  FSMC_AF);
    gpio_af_set(GPIOG, 3,  FSMC_AF);
    gpio_af_set(GPIOG, 4,  FSMC_AF);
    gpio_af_set(GPIOG, 5,  FSMC_AF);
    gpio_af_set(GPIOG, 10, FSMC_AF);

    gpio_set(GPIOG,
             (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) |
             (1 << 4) | (1 << 5) | (1 << 10),
             2, 0, GPIO_SPEED_FREQ_VERY_HIGH, 0);
}

/* ============================================================================
 * FSMC 控制器配置（LL 驱动）
 * ============================================================================ */
static void bsp_sram_fsmc_init(void)
{
    FSMC_NORSRAM_TimingTypeDef timing = {0};
    FSMC_NORSRAM_InitTypeDef  init   = {0};

    /* 使能 FSMC 时钟 */
    __HAL_RCC_FSMC_CLK_ENABLE();

    /* ---- 时序配置 (HCLK = 168MHz → 1 tick ≈ 5.95ns) ---- */
    timing.AddressSetupTime      = 3;     /* t_AS  ≈ 17.9ns */
    timing.AddressHoldTime       = 1;     /* t_AH  = 1 HCLK */
    timing.DataSetupTime         = 3;     /* t_DS  ≈ 23.8ns */
    timing.BusTurnAroundDuration = 1;
    timing.CLKDivision           = 0;
    timing.DataLatency           = 0;
    timing.AccessMode            = FSMC_ACCESS_MODE_A;

    /* ---- 基本配置 ---- */
    init.NSBank                  = FSMC_NORSRAM_BANK3;
    init.DataAddressMux          = FSMC_DATA_ADDRESS_MUX_DISABLE;
    init.MemoryType              = FSMC_MEMORY_TYPE_SRAM;
    init.MemoryDataWidth         = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    init.BurstAccessMode         = FSMC_BURST_ACCESS_MODE_DISABLE;
    init.WaitSignalPolarity      = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    init.WrapMode                = FSMC_WRAP_MODE_DISABLE;
    init.WaitSignalActive        = FSMC_WAIT_TIMING_BEFORE_WS;
    init.WriteOperation          = FSMC_WRITE_OPERATION_ENABLE;
    init.WaitSignal              = FSMC_WAIT_SIGNAL_DISABLE;
    init.ExtendedMode            = FSMC_EXTENDED_MODE_DISABLE;
    init.AsynchronousWait        = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    init.WriteBurst              = FSMC_WRITE_BURST_DISABLE;
    init.ContinuousClock         = FSMC_CONTINUOUS_CLOCK_SYNC_ONLY;
    init.PageSize                = FSMC_PAGE_SIZE_NONE;

    /* ---- 写入控制器寄存器 ---- */
    FSMC_NORSRAM_Init(FSMC_NORSRAM_DEVICE, &init);
    FSMC_NORSRAM_Timing_Init(FSMC_NORSRAM_DEVICE, &timing, FSMC_NORSRAM_BANK3);

    /* 使能 Bank3 */
    __FSMC_NORSRAM_ENABLE(FSMC_NORSRAM_DEVICE, FSMC_NORSRAM_BANK3);
}

/* ============================================================================
 * 初始化
 * ============================================================================ */
void bsp_sram_init(void)
{
    bsp_sram_gpio_init();
    bsp_sram_fsmc_init();
}

/* ============================================================================
 * 基本读写
 * ============================================================================ */
void     bsp_sram_write8(uint32_t addr, uint8_t data)   { *SRAM_PTR(addr) = data; }
uint8_t  bsp_sram_read8(uint32_t addr)                   { return *SRAM_PTR(addr); }
void     bsp_sram_write16(uint32_t addr, uint16_t data)  { *SRAM_PTR16(addr) = data; }
uint16_t bsp_sram_read16(uint32_t addr)                  { return *SRAM_PTR16(addr); }
void     bsp_sram_write32(uint32_t addr, uint32_t data)  { *SRAM_PTR32(addr) = data; }
uint32_t bsp_sram_read32(uint32_t addr)                  { return *SRAM_PTR32(addr); }

/* ============================================================================
 * 块操作
 * ============================================================================ */
void bsp_sram_write_buf(uint32_t addr, const uint8_t *buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        *SRAM_PTR(addr + i) = buf[i];
}

void bsp_sram_read_buf(uint32_t addr, uint8_t *buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        buf[i] = *SRAM_PTR(addr + i);
}

void bsp_sram_memset(uint32_t addr, uint8_t data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
        *SRAM_PTR(addr + i) = data;
}

void bsp_sram_memcpy(uint32_t dst, uint32_t src, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
        *SRAM_PTR(dst + i) = *SRAM_PTR(src + i);
}

void bsp_sram_clean(void)
{
    volatile uint32_t *p;

    if (bsp_sram_probe() != 0) return;

    p = SRAM_PTR32(0);
    for (uint32_t i = 0; i < EXT_SRAM_SIZE / 4; i++)
        p[i] = 0;
}

/* ============================================================================
 * 存在性探针
 * ============================================================================ */
int bsp_sram_probe(void)
{
    volatile uint32_t *p32 = SRAM_PTR32(0);

    p32[0] = 0xA5A55A5A;
    if (p32[0] != 0xA5A55A5A)
        return 1;

    p32[0] = 0x5AA5A55A;
    if (p32[0] != 0x5AA5A55A)
        return 2;

    return 0;
}

/* ============================================================================
 * March C- 详细全扫描（扫盘指令）
 *
 * Algorithm:
 *   M1: up write i                     -- init
 *   M2: up read i, write ~i            -- detect coupling + transition(0->1)
 *   M3: down read ~i, write i          -- reverse coupling + transition(1->0)
 *   M4: down read i, write ~i          -- reverse again
 *   M5: up read ~i, write i            -- forward again
 *   M6: up read i                      -- final check
 *   NBL: byte-by-byte [0x55,0xA5,0x5A,0xAA] -- byte strobe
 * ============================================================================ */
void bsp_sram_scan(void)
{
    volatile uint32_t *p32;
    volatile uint8_t  *p8;
    uint32_t i;
    uint32_t total_words = EXT_SRAM_SIZE / 4;
    uint32_t blk_words = 256;
    uint32_t total_blks = total_words / blk_words;
    uint32_t total_errs = 0;
    uint32_t bad_blks = 0;
    uint32_t blk_start, blk_end;

    if (bsp_sram_probe() != 0)
    {
        bsp_usart_printf(&s_usart1, "[SRAM SCAN] Probe FAILED\r\n");
        return;
    }

    p32 = SRAM_PTR32(0);
    p8  = SRAM_PTR(0);

    /* M1: up write i */
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] M1: write i (ascending)...\r\n");
    for (i = 0; i < total_words; i++) p32[i] = i;
    HAL_Delay(10);

    /* M2: up read i, write ~i */
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] M2: read i, write ~i (ascending)...\r\n");
    for (uint32_t blk = 0; blk < total_blks; blk++)
    {
        uint32_t blk_errs = 0;
        uint32_t first_idx = 0, first_exp = 0, first_got = 0;
        blk_start = blk * blk_words;
        blk_end   = blk_start + blk_words;

        for (i = blk_start; i < blk_end; i++)
        {
            uint32_t got1 = p32[i];
            if (got1 != i)
            {
                uint32_t got2 = p32[i];
                if (got1 == got2)
                {
                    if (blk_errs == 0) { first_idx = i; first_exp = i; first_got = got1; }
                    blk_errs++;
                }
                else
                {
                    bsp_usart_printf(&s_usart1, "[SRAM SCAN][M2] GLITCH: addr=0x%08lX, got1=0x%08lX, got2=0x%08lX (ignored)\r\n",
                                     EXT_SRAM_ADDR + i * 4, got1, got2);
                }
            }
            p32[i] = ~i;
        }

        if (blk_errs > 0)
        {
            bsp_usart_printf(&s_usart1, "[SRAM SCAN][M2] BLK: blk=%lu, addr=0x%08lX, "
                             "first=0x%08lX, exp=0x%08lX, got=0x%08lX, errs=%lu\r\n",
                             blk, EXT_SRAM_ADDR + blk_start * 4,
                             EXT_SRAM_ADDR + first_idx * 4,
                             first_exp, first_got, blk_errs);
            total_errs += blk_errs; bad_blks++;
        }
    }

    /* M3: down read ~i, write i */
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] M3: read ~i, write i (descending)...\r\n");
    for (int32_t blk = (int32_t)total_blks - 1; blk >= 0; blk--)
    {
        uint32_t blk_errs = 0;
        uint32_t first_idx = 0, first_exp = 0, first_got = 0;
        blk_start = (uint32_t)blk * blk_words;
        blk_end   = blk_start + blk_words;

        for (i = blk_end; i > blk_start; )
        {
            i--;
            uint32_t got1 = p32[i];
            if (got1 != ~i)
            {
                uint32_t got2 = p32[i];
                if (got1 == got2)
                {
                    if (blk_errs == 0) { first_idx = i; first_exp = ~i; first_got = got1; }
                    blk_errs++;
                }
                else
                {
                    bsp_usart_printf(&s_usart1, "[SRAM SCAN][M3] GLITCH: addr=0x%08lX, got1=0x%08lX, got2=0x%08lX (ignored)\r\n",
                                     EXT_SRAM_ADDR + i * 4, got1, got2);
                }
            }
            p32[i] = i;
        }

        if (blk_errs > 0)
        {
            bsp_usart_printf(&s_usart1, "[SRAM SCAN][M3] BLK: blk=%lu, addr=0x%08lX, "
                             "first=0x%08lX, exp=0x%08lX, got=0x%08lX, errs=%lu\r\n",
                             (uint32_t)blk, EXT_SRAM_ADDR + blk_start * 4,
                             EXT_SRAM_ADDR + first_idx * 4,
                             first_exp, first_got, blk_errs);
            total_errs += blk_errs; bad_blks++;
        }
    }

    /* M4: down read i, write ~i */
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] M4: read i, write ~i (descending)...\r\n");
    for (int32_t blk = (int32_t)total_blks - 1; blk >= 0; blk--)
    {
        uint32_t blk_errs = 0;
        uint32_t first_idx = 0, first_exp = 0, first_got = 0;
        blk_start = (uint32_t)blk * blk_words;
        blk_end   = blk_start + blk_words;

        for (i = blk_end; i > blk_start; )
        {
            i--;
            uint32_t got1 = p32[i];
            if (got1 != i)
            {
                uint32_t got2 = p32[i];
                if (got1 == got2)
                {
                    if (blk_errs == 0) { first_idx = i; first_exp = i; first_got = got1; }
                    blk_errs++;
                }
                else
                {
                    bsp_usart_printf(&s_usart1, "[SRAM SCAN][M4] GLITCH: addr=0x%08lX, got1=0x%08lX, got2=0x%08lX (ignored)\r\n",
                                     EXT_SRAM_ADDR + i * 4, got1, got2);
                }
            }
            p32[i] = ~i;
        }

        if (blk_errs > 0)
        {
            bsp_usart_printf(&s_usart1, "[SRAM SCAN][M4] BLK: blk=%lu, addr=0x%08lX, "
                             "first=0x%08lX, exp=0x%08lX, got=0x%08lX, errs=%lu\r\n",
                             (uint32_t)blk, EXT_SRAM_ADDR + blk_start * 4,
                             EXT_SRAM_ADDR + first_idx * 4,
                             first_exp, first_got, blk_errs);
            total_errs += blk_errs; bad_blks++;
        }
    }

    /* M5: up read ~i, write i */
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] M5: read ~i, write i (ascending)...\r\n");
    for (uint32_t blk = 0; blk < total_blks; blk++)
    {
        uint32_t blk_errs = 0;
        uint32_t first_idx = 0, first_exp = 0, first_got = 0;
        blk_start = blk * blk_words;
        blk_end   = blk_start + blk_words;

        for (i = blk_start; i < blk_end; i++)
        {
            uint32_t got1 = p32[i];
            if (got1 != ~i)
            {
                uint32_t got2 = p32[i];
                if (got1 == got2)
                {
                    if (blk_errs == 0) { first_idx = i; first_exp = ~i; first_got = got1; }
                    blk_errs++;
                }
                else
                {
                    bsp_usart_printf(&s_usart1, "[SRAM SCAN][M5] GLITCH: addr=0x%08lX, got1=0x%08lX, got2=0x%08lX (ignored)\r\n",
                                     EXT_SRAM_ADDR + i * 4, got1, got2);
                }
            }
            p32[i] = i;
        }

        if (blk_errs > 0)
        {
            bsp_usart_printf(&s_usart1, "[SRAM SCAN][M5] BLK: blk=%lu, addr=0x%08lX, "
                             "first=0x%08lX, exp=0x%08lX, got=0x%08lX, errs=%lu\r\n",
                             blk, EXT_SRAM_ADDR + blk_start * 4,
                             EXT_SRAM_ADDR + first_idx * 4,
                             first_exp, first_got, blk_errs);
            total_errs += blk_errs; bad_blks++;
        }
    }
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] Retention delay 10ms...\r\n");
    HAL_Delay(10);

    /* M6: up read i */
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] M6: read i (ascending)...\r\n");
    for (uint32_t blk = 0; blk < total_blks; blk++)
    {
        uint32_t blk_errs = 0;
        uint32_t first_idx = 0, first_exp = 0, first_got = 0;
        blk_start = blk * blk_words;
        blk_end   = blk_start + blk_words;

        for (i = blk_start; i < blk_end; i++)
        {
            uint32_t got1 = p32[i];
            if (got1 != i)
            {
                uint32_t got2 = p32[i];
                if (got1 == got2)
                {
                    if (blk_errs == 0) { first_idx = i; first_exp = i; first_got = got1; }
                    blk_errs++;
                }
                else
                {
                    bsp_usart_printf(&s_usart1, "[SRAM SCAN][M6] GLITCH: addr=0x%08lX, got1=0x%08lX, got2=0x%08lX (ignored)\r\n",
                                     EXT_SRAM_ADDR + i * 4, got1, got2);
                }
            }
        }

        if (blk_errs > 0)
        {
            bsp_usart_printf(&s_usart1, "[SRAM SCAN][M6] BLK: blk=%lu, addr=0x%08lX, "
                             "first=0x%08lX, exp=0x%08lX, got=0x%08lX, errs=%lu\r\n",
                             blk, EXT_SRAM_ADDR + blk_start * 4,
                             EXT_SRAM_ADDR + first_idx * 4,
                             first_exp, first_got, blk_errs);
            total_errs += blk_errs; bad_blks++;
        }
    }

    /* NBL: byte pattern test */
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] NBL: byte pattern test...\r\n");
    for (i = 0; i < total_words; i++)
    {
        uint32_t a = i * 4;
        p8[a]     = 0x55;
        p8[a + 1] = 0xA5;
        p8[a + 2] = 0x5A;
        p8[a + 3] = 0xAA;
    }
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] Retention delay 10ms...\r\n");
    HAL_Delay(10);
    bsp_usart_printf(&s_usart1, "[SRAM SCAN] NBL: verifying...\r\n");

    for (uint32_t blk = 0; blk < total_blks; blk++)
    {
        uint32_t blk_errs = 0;
        uint32_t first_idx = 0, first_exp = 0, first_got = 0;
        blk_start = blk * blk_words;
        blk_end   = blk_start + blk_words;

        for (i = blk_start; i < blk_end; i++)
        {
            uint32_t a = i * 4;

            {
                uint8_t g1 = p8[a];
                if (g1 != 0x55) {
                    uint8_t g2 = p8[a];
                    if (g1 == g2) {
                        if (blk_errs == 0) { first_idx = a; first_exp = 0x55; first_got = g1; }
                        blk_errs++;
                    } else {
                        bsp_usart_printf(&s_usart1, "[SRAM SCAN][NBL] GLITCH: addr=0x%08lX, got1=0x%02X, got2=0x%02X (ignored)\r\n",
                                         EXT_SRAM_ADDR + a, g1, g2);
                    }
                }
            }
            {
                uint8_t g1 = p8[a + 1];
                if (g1 != 0xA5) {
                    uint8_t g2 = p8[a + 1];
                    if (g1 == g2) {
                        if (blk_errs == 0) { first_idx = a + 1; first_exp = 0xA5; first_got = g1; }
                        blk_errs++;
                    } else {
                        bsp_usart_printf(&s_usart1, "[SRAM SCAN][NBL] GLITCH: addr=0x%08lX, got1=0x%02X, got2=0x%02X (ignored)\r\n",
                                         EXT_SRAM_ADDR + a + 1, g1, g2);
                    }
                }
            }
            {
                uint8_t g1 = p8[a + 2];
                if (g1 != 0x5A) {
                    uint8_t g2 = p8[a + 2];
                    if (g1 == g2) {
                        if (blk_errs == 0) { first_idx = a + 2; first_exp = 0x5A; first_got = g1; }
                        blk_errs++;
                    } else {
                        bsp_usart_printf(&s_usart1, "[SRAM SCAN][NBL] GLITCH: addr=0x%08lX, got1=0x%02X, got2=0x%02X (ignored)\r\n",
                                         EXT_SRAM_ADDR + a + 2, g1, g2);
                    }
                }
            }
            {
                uint8_t g1 = p8[a + 3];
                if (g1 != 0xAA) {
                    uint8_t g2 = p8[a + 3];
                    if (g1 == g2) {
                        if (blk_errs == 0) { first_idx = a + 3; first_exp = 0xAA; first_got = g1; }
                        blk_errs++;
                    } else {
                        bsp_usart_printf(&s_usart1, "[SRAM SCAN][NBL] GLITCH: addr=0x%08lX, got1=0x%02X, got2=0x%02X (ignored)\r\n",
                                         EXT_SRAM_ADDR + a + 3, g1, g2);
                    }
                }
            }
        }

        if (blk_errs > 0)
        {
            bsp_usart_printf(&s_usart1, "[SRAM SCAN][NBL] BLK: blk=%lu, addr=0x%08lX, "
                             "first=0x%08lX, exp=0x%08lX, got=0x%08lX, errs=%lu\r\n",
                             blk, EXT_SRAM_ADDR + blk_start * 4,
                             EXT_SRAM_ADDR + first_idx,
                             first_exp, first_got, blk_errs);
            total_errs += blk_errs; bad_blks++;
        }
    }

    /* result */
    if (total_errs == 0)
        bsp_usart_printf(&s_usart1, "[SRAM SCAN] PASS - no errors\r\n");
    else
        bsp_usart_printf(&s_usart1, "[SRAM SCAN] FAIL - total %lu errors in %lu blocks\r\n",
                         total_errs, bad_blks);
}
