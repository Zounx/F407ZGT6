/**
 ****************************************************************************************************
 * @file        bsp_adc.c
 * @brief       BSP — RTC 实时时钟 & 内置温度传感器驱动实现（F407 版）
 * @note        RTC：LSE 32.768 kHz → PREDIV_A=127 → 256 Hz → PREDIV_S=255 → 1 Hz
 *              温度传感器：ADC1 通道 16，采样时间 ≥ 480 cycles
 *              VBAT：ADC1 通道 18（ADC1_IN18），分压比 1/2
 ****************************************************************************************************
 */

#include "bsp_adc.h"
#include "bsp_usart.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>

/* ==========================================================================
 * 局部变量
 * ========================================================================== */

static RTC_HandleTypeDef s_rtc_handle = {0};
static uint8_t g_adc1_inited = 0;       /* ADC1 温度传感器初始化标志 */

/* USART1 对象引用（Main.c 中定义，用于 bsp_usart_printf） */
extern struct bsp_usart s_usart1;

/* F407 CMSIS 缺少 CAL/RSTCAL 位定义，手动补全 */
#ifndef ADC_CR2_CAL
#define ADC_CR2_CAL     (1UL << 2)   /* Calibration */
#define ADC_CR2_RSTCAL  (1UL << 3)   /* Reset Calibration */
#endif

/* ==========================================================================
 * RTC 实时时钟
 * ========================================================================== */

/**
 * @brief  使能备份域写访问（PWR CR.DBP）
 * @note   F4 需要先使能 PWR 时钟（PWR 挂在 APB1）
 */
static void pwr_backup_access_enable(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}

/**
 * @brief  检查日历是否已初始化（INITS 标志）
 * @retval 0=未初始化，1=已初始化
 */
static int rtc_is_calendar_inited(void)
{
    return (s_rtc_handle.Instance->ISR & RTC_ISR_INITS) ? 1 : 0;
}

/**
 * @brief  设置默认初始时间（2026-07-01 00:00:00 周三）
 */
static void rtc_set_default_calendar(void)
{
    RTC_DateTypeDef date;
    RTC_TimeTypeDef time;

    /* 日期：2026-07-01 星期三 */
    date.Year    = 26;        /* 0~99 */
    date.Month   = 7;
    date.Date    = 1;
    date.WeekDay = 3;         /* 1=周一 ~ 7=周日，3=周三 */

    /* 时间：00:00:00 */
    time.Hours   = 0;
    time.Minutes = 0;
    time.Seconds = 0;
    time.TimeFormat = RTC_HOURFORMAT_24;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;

    HAL_RTC_SetDate(&s_rtc_handle, &date, RTC_FORMAT_BIN);
    HAL_RTC_SetTime(&s_rtc_handle, &time, RTC_FORMAT_BIN);
}

void rtc_init(void)
{
    RCC_OscInitTypeDef osc_cfg = {0};
    RCC_PeriphCLKInitTypeDef per_clk = {0};

    /* 1. 使能备份域写访问 */
    pwr_backup_access_enable();

    /* 2. 启动 LSE 振荡器 */
    osc_cfg.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc_cfg.LSEState       = RCC_LSE_ON;
    if (HAL_RCC_OscConfig(&osc_cfg) != HAL_OK)
    {
        /* LSE 启动失败，后续 rtc_is_calendar_inited 返回 0 */
        return;
    }

    /* 3. 选择 LSE 为 RTC 时钟源，使能 RTC */
    per_clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    per_clk.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig(&per_clk);

    /* 4. 使能 RTC 时钟 */
    __HAL_RCC_RTC_ENABLE();

    /* 5. 初始化 RTC 句柄 */
    s_rtc_handle.Instance          = RTC;
    s_rtc_handle.Init.HourFormat   = RTC_HOURFORMAT_24;
    s_rtc_handle.Init.AsynchPrediv = 127;   /* PREDIV_A = 127 */
    s_rtc_handle.Init.SynchPrediv  = 255;   /* PREDIV_S = 255 */
    s_rtc_handle.Init.OutPut       = RTC_OUTPUT_DISABLE;
    s_rtc_handle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_rtc_handle.Init.OutPutType   = RTC_OUTPUT_TYPE_OPENDRAIN;

    if (HAL_RTC_Init(&s_rtc_handle) != HAL_OK)
        return;

    /* 6. 如果日历未初始化（备份域复位后或首次上电），设置默认时间 */
    if (!rtc_is_calendar_inited())
    {
        rtc_set_default_calendar();
    }
}

void rtc_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    RTC_TimeTypeDef time;

    if (s_rtc_handle.Instance == NULL)
        return;

    HAL_RTC_GetTime(&s_rtc_handle, &time, RTC_FORMAT_BIN);
    /* 需要顺便读一次 RTC_DR 以解锁影子寄存器 */
    (void)s_rtc_handle.Instance->DR;

    if (hour)   *hour   = time.Hours;
    if (minute) *minute = time.Minutes;
    if (second) *second = time.Seconds;
}

void rtc_get_date(uint8_t *year, uint8_t *month, uint8_t *day, uint8_t *weekday)
{
    RTC_DateTypeDef date;

    if (s_rtc_handle.Instance == NULL)
        return;

    HAL_RTC_GetDate(&s_rtc_handle, &date, RTC_FORMAT_BIN);

    if (year)    *year    = date.Year;
    if (month)   *month   = date.Month;
    if (day)     *day     = date.Date;
    if (weekday) *weekday = date.WeekDay;
}

void rtc_get_time_str(char *buf)
{
    uint8_t h, m, s;
    rtc_get_time(&h, &m, &s);
    sprintf(buf, "%02u:%02u:%02u", h, m, s);
}

void rtc_get_date_str(char *buf)
{
    uint8_t y, m, d, w;
    rtc_get_date(&y, &m, &d, &w);
    sprintf(buf, "20%02u-%02u-%02u %u", y, m, d, w);
}

/* ==========================================================================
 * 内置温度传感器（ADC1 通道 16）
 * ========================================================================== */

static ADC_HandleTypeDef s_adc1_handle = {0};

/**
 * @brief  初始化 ADC1 用于温度传感器测量
 * @note   F407 ADC1 可采集内部通道：温度传感器（通道16）和 VBAT（通道18）
 *         通过 ADC123_COMMON->CCR 的 TSVREFE/VBATE 位使能
 */
static void adc1_temp_init(void)
{
    ADC_ChannelConfTypeDef ch_cfg = {0};
    uint32_t wait_loop;

    if (g_adc1_inited)
        return;

    /* 使能 ADC1 时钟（APB2） */
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* ADC1 基本配置（F4 无 LowPowerAutoWait / Overrun / LeftBitShift / Oversampling 等字段） */
    s_adc1_handle.Instance             = ADC1;
    s_adc1_handle.Init.ClockPrescaler  = ADC_CLOCK_SYNC_PCLK_DIV4;   /* APB2=84MHz, 分频后 21MHz */
    s_adc1_handle.Init.Resolution      = ADC_RESOLUTION_12B;
    s_adc1_handle.Init.ScanConvMode    = DISABLE;
    s_adc1_handle.Init.EOCSelection    = ADC_EOC_SINGLE_CONV;
    s_adc1_handle.Init.ContinuousConvMode  = DISABLE;
    s_adc1_handle.Init.NbrOfConversion     = 1;
    s_adc1_handle.Init.DiscontinuousConvMode = DISABLE;
    s_adc1_handle.Init.NbrOfDiscConversion = 1;
    s_adc1_handle.Init.ExternalTrigConv    = ADC_SOFTWARE_START;
    s_adc1_handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    s_adc1_handle.Init.DataAlign        = ADC_DATAALIGN_RIGHT;
    s_adc1_handle.Init.DMAContinuousRequests = DISABLE;

    if (HAL_ADC_Init(&s_adc1_handle) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[ADC1] HAL_ADC_Init FAILED\r\n");
        return;
    }

    /* 使能温度传感器和 VBAT 通道（F4 通过 ADC123_COMMON->CCR 控制） */
    ADC123_COMMON->CCR |= (ADC_CCR_TSVREFE | ADC_CCR_VBATE);

    /* 配置通道 16（温度传感器，采样时间 480 cycles） */
    ch_cfg.Channel      = ADC_CHANNEL_TEMPSENSOR;  /* 16 */
    ch_cfg.Rank         = 1;
    ch_cfg.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    ch_cfg.Offset       = 0;

    if (HAL_ADC_ConfigChannel(&s_adc1_handle, &ch_cfg) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[ADC1] HAL_ADC_ConfigChannel FAILED\r\n");
        return;
    }

    /* 校准 ADC（F4 无 HAL_ADCEx_Calibration_Start，直接操作 ADC_CR2 寄存器） */
    /* 复位校准 */
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    wait_loop = 0;
    while ((ADC1->CR2 & ADC_CR2_RSTCAL) != 0)
    {
        wait_loop++;
        if (wait_loop > 10000)
        {
            bsp_usart_printf(&s_usart1, "[ADC1] RSTCAL timeout\r\n");
            return;
        }
    }

    /* 启动校准 */
    ADC1->CR2 |= ADC_CR2_CAL;
    wait_loop = 0;
    while ((ADC1->CR2 & ADC_CR2_CAL) != 0)
    {
        wait_loop++;
        if (wait_loop > 10000)
        {
            bsp_usart_printf(&s_usart1, "[ADC1] CAL timeout\r\n");
            return;
        }
    }

    g_adc1_inited = 1;
}

int32_t chip_vbat_read(void)
{
    HAL_StatusTypeDef st;
    ADC_ChannelConfTypeDef ch_cfg = {0};
    uint32_t sum = 0;
    uint32_t adc_val;
    int i;

    /* 确保 ADC1 已初始化 */
    if (!g_adc1_inited)
    {
        adc1_temp_init();
        if (!g_adc1_inited)
            return -1;
    }

    /* 配置通道 18 = VBAT/2 */
    ch_cfg.Channel      = ADC_CHANNEL_VBAT;        /* 18 */
    ch_cfg.Rank         = 1;
    ch_cfg.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    ch_cfg.Offset       = 0;

    if (HAL_ADC_ConfigChannel(&s_adc1_handle, &ch_cfg) != HAL_OK)
        return -1;

    /* 连续采样 8 次取平均 */
    for (i = 0; i < 8; i++)
    {
        st = HAL_ADC_Start(&s_adc1_handle);
        if (st != HAL_OK) return -1;

        st = HAL_ADC_PollForConversion(&s_adc1_handle, 100);
        if (st != HAL_OK)
        {
            HAL_ADC_Stop(&s_adc1_handle);
            return -1;
        }

        sum += HAL_ADC_GetValue(&s_adc1_handle);
        HAL_ADC_Stop(&s_adc1_handle);
    }
    adc_val = sum / 8;

    /* 切回温度传感器通道，保持 chip_temp_read() 可用 */
    ch_cfg.Channel = ADC_CHANNEL_TEMPSENSOR;
    HAL_ADC_ConfigChannel(&s_adc1_handle, &ch_cfg);

    /* VBAT = ADC × 3.3V / 4096 × 2（分压比 1/2），返回 mV */
    return (int32_t)(adc_val * 3300ULL * 2 / 4096);
}

int32_t chip_temp_read(void)
{
    uint16_t ts_cal1, ts_cal2, ts_data;
    int32_t temp100;
    HAL_StatusTypeDef st;
    uint32_t sum = 0;
    int i;

    /* 读取出厂校准值（F4 为 12 位右对齐，直接读取） */
    ts_cal1 = *((uint16_t *)0x1FFF7A2C);
    ts_cal2 = *((uint16_t *)0x1FFF7A2E);

    /* 初始化 ADC1（首次调用时） */
    if (!g_adc1_inited)
    {
        adc1_temp_init();
        if (!g_adc1_inited)
        {
            bsp_usart_printf(&s_usart1, "[CHIP_TEMP] ADC1 init failed, cannot read temp\r\n");
            return -30000;
        }
    }

    /* 连续采样 8 次后取平均（抑制 ADC 噪声） */
    for (i = 0; i < 8; i++)
    {
        st = HAL_ADC_Start(&s_adc1_handle);
        if (st != HAL_OK)
        {
            bsp_usart_printf(&s_usart1, "[ADC1] HAL_ADC_Start FAILED (%d)\r\n", (int)st);
            return -30000;
        }

        st = HAL_ADC_PollForConversion(&s_adc1_handle, 100);
        if (st != HAL_OK)
        {
            bsp_usart_printf(&s_usart1, "[ADC1] PollForConversion FAILED (%d)\r\n", (int)st);
            HAL_ADC_Stop(&s_adc1_handle);
            return -30000;
        }

        sum += HAL_ADC_GetValue(&s_adc1_handle);
        HAL_ADC_Stop(&s_adc1_handle);
    }
    ts_data = (uint16_t)(sum / 8);

    /* 温度 = ((110-30) * (TS_DATA - TS_CAL1)) / (TS_CAL2 - TS_CAL1) + 30
     * TS_CAL2 出厂标定温度为 110°C（STM32F4 系列）
     * 放大 100 倍保持精度 */
    if (ts_cal2 == ts_cal1)
        return 2500;    /* 防止除零，返回默认 25°C */

    temp100 = ((int32_t)(110 - 30) * (int32_t)(ts_data - ts_cal1) * 100)
              / (int32_t)(ts_cal2 - ts_cal1)
              + 30 * 100;

    return temp100;
}
