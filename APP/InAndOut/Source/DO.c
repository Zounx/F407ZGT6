/**
 * @file    DO.c
 * @brief   数字量输出模块实现 — 正/负逻辑 + 单脉冲输出
 *
 * 配置方法（在下方 sCfg[] 中编辑）：
 *   {port, pin, invert}
 *   - port:   GPIO 端口宏（如 GPIOA / GPIOB）
 *   - pin:    GPIO 引脚宏（如 GPIO_PIN_0 / GPIO_PIN_1）
 *   - invert: false=正逻辑（高有效），true=负逻辑（低有效）
 */

#include "DO.h"
#include "bsp_gpio.h"
#include "stm32f4xx_hal.h"   /* HAL_GetTick() */

/* ============================================================================
 *  用户配置表
 *
 *  请根据实际硬件修改此项。通道总数由数组元素个数自动确定。
 *  DO_Set() 传入的通道号从 0 开始依次对应下面每一行。
 * ==========================================================================*/
static const struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    bool          invert;    /**< true=负逻辑，false=正逻辑 */
} sCfg[] = {
    /* 通道0: 示例 — PB0, 正逻辑 */
    {GPIOB, GPIO_PIN_0,  false},

    /* ---- 以下添加更多通道 ---- */
    // {GPIOB, GPIO_PIN_1,  false},
    // {GPIOC, GPIO_PIN_0,  true},
};

#define DO_CHANNEL_COUNT  (sizeof(sCfg) / sizeof(sCfg[0]))


/* ============================================================================
 *  运行时状态
 * ==========================================================================*/

/** 每个通道的 bsp_gpio 对象 */
static struct bsp_gpio    sGpio[DO_CHANNEL_COUNT];

/** 当前输出状态（经过正/负逻辑后的有效电平） */
static bool               sState[DO_CHANNEL_COUNT];

/** 脉冲结束时间（0 = 无脉冲） */
static uint32_t           sPulseEnd[DO_CHANNEL_COUNT];


/* ============================================================================
 *  内部函数
 * ==========================================================================*/

/**
 * @brief  将有效电平转换为 GPIO 物理电平
 * @param  ch     通道号
 * @param  state  有效电平（true=有效，false=无效）
 * @return true=GPIO 高电平，false=GPIO 低电平
 */
static inline bool ToPhys(uint8_t ch, bool state)
{
    return sCfg[ch].invert ? !state : state;
}


/* ============================================================================
 *  API 实现
 * ==========================================================================*/

void DO_Init(void)
{
    uint8_t i;

    for (i = 0; i < DO_CHANNEL_COUNT; i++) {
        bsp_gpio_init(&sGpio[i], sCfg[i].port, sCfg[i].pin,
                      GPIO_MODE_OUTPUT_PP);
        sState[i]    = false;
        sPulseEnd[i] = 0;
    }
}


int DO_Set(uint8_t ch, bool state)
{
    if (ch >= DO_CHANNEL_COUNT)
        return -1;

    sPulseEnd[ch] = 0;                  /* 取消正在进行的脉冲 */
    sState[ch] = state;
    bsp_gpio_set(&sGpio[ch], ToPhys(ch, state));
    return 0;
}


bool DO_Get(uint8_t ch)
{
    if (ch >= DO_CHANNEL_COUNT)
        return false;
    return sState[ch];
}


int DO_Toggle(uint8_t ch)
{
    if (ch >= DO_CHANNEL_COUNT)
        return -1;
    return DO_Set(ch, !sState[ch]);
}


void DO_Pulse(uint8_t ch, uint16_t ms)
{
    if (ch >= DO_CHANNEL_COUNT)
        return;

    sPulseEnd[ch] = HAL_GetTick() + ms;
    sState[ch] = true;
    bsp_gpio_set(&sGpio[ch], ToPhys(ch, true));
}


void DO_Process(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t i;

    for (i = 0; i < DO_CHANNEL_COUNT; i++) {
        if (sPulseEnd[i] != 0 && now >= sPulseEnd[i]) {
            /* 脉冲结束，恢复无效电平 */
            sPulseEnd[i] = 0;
            sState[i] = false;
            bsp_gpio_set(&sGpio[i], ToPhys(i, false));
        }
    }
}
