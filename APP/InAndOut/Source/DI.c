/**
 * @file    DI.c
 * @brief   数字量输入模块实现 — 完整流水线
 *
 * 流水线：
 *   1. 读取原始 GPIO 电平
 *   2. LogicDeal：正/负逻辑处理
 *   3. Filtr：等长防抖滤波（高/低独立计数）
 *   4. Delay：ON/OFF 独立延时
 *   5. 输出最终结果
 *
 * 配置方法：
 *   在下方 sCfg[] 中编辑每个通道的参数。
 *   通道总数由数组元素个数自动确定。
 *
 */

#include "DI.h"
#include "bsp_gpio.h"

/* ============================================================================
 *  通道配置表
 *
 *  每项含义：
 *    port          GPIO 端口（如 GPIOA）
 *    pin           GPIO 引脚（如 GPIO_PIN_0）
 *    invert        逻辑处理：false=正逻辑(NO)，true=负逻辑(NC)
 *    filtrEnable   是否滤波：false=不滤波，true=滤波
 *    filtrTime     滤波时间（处理周期数）
 *    delayEnable   是否延时：false=不延时，true=延时
 *    enDelayTime   有效延时（ON 延时，处理周期数）
 *    disDelayTime  无效延时（OFF 延时，处理周期数）
 *
 *   时间单位 = DI_Process() 的调用周期。
 *   例如 DI_Process 每 10ms 调用一次，filtrTime=5 → 50ms 滤波。
 * ==========================================================================*/
static const struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    bool          invert;
    bool          filtrEnable;
    uint16_t      filtrTime;
    bool          delayEnable;
    uint16_t      enDelayTime;
    uint16_t      disDelayTime;
} sCfg[] = {
    /*    port    pin          invert filtrEn  fltT  delayEn  enDly  disDly */
    { GPIOA,     GPIO_PIN_0,  false, true,     5,   true,     3,     2     },
    // { GPIOA,  GPIO_PIN_1,  false, true,     5,   true,     5,     5     },
};

#define DI_CHANNEL_COUNT  (sizeof(sCfg) / sizeof(sCfg[0]))


/* ============================================================================
 *  通道运行时状态
 * ==========================================================================*/

struct DiVar {
    uint16_t  tmp;             /* DI_Tmp — 滤波后的中间结果 */
    uint16_t  old;             /* DI_Old — 防抖上一次结果（用于滤波比较） */
    uint16_t  old2;            /* DI_Old2 — 延时上一次结果（用于延时比较） */
    uint16_t  fltCntEn;       /* DI_FiltrCntEn — 防抖计数有效（高电平稳定计数） */
    uint16_t  fltCntDis;      /* DI_FiltrCntDis — 防抖计数无效（低电平稳定计数） */
    uint16_t  dlyCntEn;       /* DI_DelayCntEn — 延时计数有效（ON 延时计数） */
    uint16_t  dlyCntDis;      /* DI_DelayCntDis — 延时计数无效（OFF 延时计数） */
    uint16_t  out;             /* MyDI_Out — 最终输出 */
};

static struct bsp_gpio    sGpio[DI_CHANNEL_COUNT];
static struct DiVar       sVar[DI_CHANNEL_COUNT];


/* ============================================================================
 *  内部函数
 * ==========================================================================*/

/**
 * @brief  读取通道原始 GPIO 电平
 */
static inline uint16_t ReadRaw(uint8_t ch)
{
    return bsp_gpio_get(&sGpio[ch]) ? 1 : 0;
}


/**
 * @brief  滤波处理
 *
 * 等长防抖：连续 filtrTime 次采样相同才确认切换。
 * 高/低电平各有一个独立计数器。
 */
static void DiFiltr(uint8_t ch)
{
    uint16_t        raw  = ReadRaw(ch);
    uint16_t        proc = sCfg[ch].invert ? !raw : raw;
    uint16_t        ft   = sCfg[ch].filtrTime;
    struct DiVar   *var  = &sVar[ch];

    if (proc == var->old) {
        /* 输入稳定 */
        if (proc == 1) {
            if (var->fltCntEn < ft) {
                var->fltCntEn++;
                /* 计数未完成，tmp 保持原值 */
            } else {
                var->tmp = 1;
            }
        } else {
            if (var->fltCntDis < ft) {
                var->fltCntDis++;
                /* 计数未完成，tmp 保持原值 */
            } else {
                var->tmp = 0;
            }
        }
    } else {
        /* 输入变化，重置两个计数器 */
        var->old = proc;
        var->fltCntEn  = 0;
        var->fltCntDis = 0;
        /* tmp 保持原值，不立即切换 */
    }

    /* 优化：无论输入是否变化，重置"另一个"计数器 */
    if (proc == 1) {
        var->fltCntDis = 0;
    } else {
        var->fltCntEn  = 0;
    }
}


/**
 * @brief  延时处理
 *
 * ON/OFF 独立延时：有效电平延时 enDelayTime 个周期后输出，
 * 无效电平延时 disDelayTime 个周期后输出。
 */
static void DiDelay(uint8_t ch)
{
    uint16_t        enDly  = sCfg[ch].enDelayTime;
    uint16_t        disDly = sCfg[ch].disDelayTime;
    uint16_t        fval   = sVar[ch].tmp;            /* 滤波后的值 */
    struct DiVar   *var    = &sVar[ch];

    if (fval == var->old2) {
        /* 滤波后值稳定 */
        if (fval == 1) {
            if (var->dlyCntEn < enDly) {
                var->dlyCntEn++;
                /* 延时未完成，输出保持原值 */
            } else {
                var->out = 1;
            }
        } else {
            if (var->dlyCntDis < disDly) {
                var->dlyCntDis++;
                /* 延时未完成，输出保持原值 */
            } else {
                var->out = 0;
            }
        }
    } else {
        /* 滤波后值变化，重置两个延时计数器 */
        var->old2 = fval;
        var->dlyCntEn  = 0;
        var->dlyCntDis = 0;
        /* 输出保持原值，不立即切换 */
    }

    /* 优化：重置"另一个"计数器 */
    if (fval == 1) {
        var->dlyCntDis = 0;
    } else {
        var->dlyCntEn  = 0;
    }
}


/* ============================================================================
 *  API 实现
 * ==========================================================================*/

void DI_Init(void)
{
    uint8_t i;

    for (i = 0; i < DI_CHANNEL_COUNT; i++) {
        bsp_gpio_init(&sGpio[i], sCfg[i].port, sCfg[i].pin,
                      GPIO_MODE_INPUT);
        sVar[i] = (struct DiVar){0};
    }
}


void DI_Process(void)
{
    uint8_t i;

    for (i = 0; i < DI_CHANNEL_COUNT; i++) {
        /* ---- 1. 滤波 ---- */
        if (sCfg[i].filtrEnable) {
            DiFiltr(i);
        } else {
            /* 不滤波：逻辑处理后的值直接作为 tmp */
            uint16_t raw = ReadRaw(i);
            sVar[i].tmp = sCfg[i].invert ? !raw : raw;
            sVar[i].old = sVar[i].tmp;
        }

        /* ---- 2. 延时 ---- */
        if (sCfg[i].delayEnable) {
            DiDelay(i);
        } else {
            /* 不延时：滤波后的 tmp 直接作为最终输出 */
            sVar[i].out  = sVar[i].tmp;
            sVar[i].old2 = sVar[i].tmp;
        }
    }
}


uint16_t DI_GetOut(uint8_t ch)
{
    if (ch >= DI_CHANNEL_COUNT)
        return 0;
    return sVar[ch].out;
}


uint16_t DI_GetRaw(uint8_t ch)
{
    if (ch >= DI_CHANNEL_COUNT)
        return 0;
    return ReadRaw(ch);
}
