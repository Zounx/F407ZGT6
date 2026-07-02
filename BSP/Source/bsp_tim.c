/**
 * @file    bsp_tim.c
 * @brief   定时器周期中断驱动实现（F407 版，支持 TIM1~14）
 *
 * ============================================================================
 *  时钟计算
 * ============================================================================
 *
 *  定时器时钟来源（F407 @168MHz）：
 *
 *     APB1 定时器（TIM2/3/4/5/6/7、TIM12/13/14）：
 *       时钟 = HAL_RCC_GetPCLK1Freq() * 2
 *       （APB1 预分频 = 4 > 1，定时器时钟 = 2 x APB1 总线时钟）
 *       F407 上 APB1 = 42MHz，所以定时器时钟 = 84MHz
 *
 *     APB2 定时器（TIM1、TIM8、TIM9/10/11）：
 *       时钟 = HAL_RCC_GetPCLK2Freq() * 2
 *       （APB2 预分频 = 2 > 1，定时器时钟 = 2 x APB2 总线时钟）
 *       F407 上 APB2 = 84MHz，所以定时器时钟 = 168MHz
 *
 *  PSC/ARR 计算（目标：tick = 1us）：
 *     APB1 定时器：PSC = 84M / 1M - 1 = 83
 *     APB2 定时器：PSC = 168M / 1M - 1 = 167
 *     ARR = period_us - 1
 *
 * ============================================================================
 *  32位 vs 16位 定时器
 * ============================================================================
 *
 *  定时器       位数    ARR 上限       tick=1us 时最大周期
 *   TIM2/5       32    0xFFFFFFFF     约 4294 秒
 *   其余(TIM1/3/ 16    0xFFFF         约 65 毫秒
 *     4/6/7/8/9/
 *     10/11/12/13/14)
 *
 * ============================================================================
 *  中断处理流程
 * ============================================================================
 *
 *  定时器计数到 ARR -> 溢出 -> 硬件置 UIF 标志 -> NVIC 触发 IRQHandler
 *   -> bsp_tim_irq_handler() 清 UIF -> 调 callback -> 返回
 *   -> 定时器继续从 0 开始计数
 */

#include "bsp_tim.h"
#include "scheduler.h"


/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief  判断定时器是否在 APB2 总线上
 */
static bool _is_apb2_timer(TIM_TypeDef *instance)
{
    return (instance == TIM1 || instance == TIM8 ||
            instance == TIM9 || instance == TIM10 || instance == TIM11);
}

/**
 * @brief  判断定时器是否有效（TIM1~14 之一）
 */
static bool _is_valid_timer(TIM_TypeDef *instance)
{
    return (instance == TIM1  || instance == TIM2  || instance == TIM3  ||
            instance == TIM4  || instance == TIM5  || instance == TIM6  ||
            instance == TIM7  || instance == TIM8  || instance == TIM9  ||
            instance == TIM10 || instance == TIM11 || instance == TIM12 ||
            instance == TIM13 || instance == TIM14);
}

/**
 * @brief  获取定时器更新中断对应的 IRQn
 * @note   映射定时器实例到其中断向量号（更新中断 UIF 触发使用）
 */
static IRQn_Type _get_tim_irqn(TIM_TypeDef *instance)
{
    if (instance == TIM1)       return TIM1_UP_TIM10_IRQn;
    else if (instance == TIM2)  return TIM2_IRQn;
    else if (instance == TIM3)  return TIM3_IRQn;
    else if (instance == TIM4)  return TIM4_IRQn;
    else if (instance == TIM5)  return TIM5_IRQn;
    else if (instance == TIM6)  return TIM6_DAC_IRQn;
    else if (instance == TIM7)  return TIM7_IRQn;
    else if (instance == TIM8)  return TIM8_UP_TIM13_IRQn;
    else if (instance == TIM9)  return TIM1_BRK_TIM9_IRQn;
    else if (instance == TIM10) return TIM1_UP_TIM10_IRQn;
    else if (instance == TIM11) return TIM1_TRG_COM_TIM11_IRQn;
    else if (instance == TIM12) return TIM8_BRK_TIM12_IRQn;
    else if (instance == TIM13) return TIM8_UP_TIM13_IRQn;
    else                        return TIM8_TRG_COM_TIM14_IRQn; /* TIM14 */
}


/* ============================================================================
 * 初始化（毫秒精度）
 * ============================================================================ */

int bsp_tim_init(struct bsp_tim *me, TIM_TypeDef *instance,
                 uint32_t period_ms, void (*callback)(void),
                 uint8_t priority)
{
    TIM_HandleTypeDef htim = {0};
    uint32_t timer_clk, psc, arr;

    /* ---------- 参数校验 ---------- */
    if (!me || !instance)
        return -1;

    if (!_is_valid_timer(instance))
        return -1;

    if (period_ms == 0)
        return -2;

    /* ---------- 保存配置 ---------- */
    me->instance  = instance;
    me->period_ms = period_ms;
    me->callback  = callback;

    /* ---------- 1. 使能定时器时钟 ---------- */
    if      (instance == TIM1)  __HAL_RCC_TIM1_CLK_ENABLE();
    else if (instance == TIM2)  __HAL_RCC_TIM2_CLK_ENABLE();
    else if (instance == TIM3)  __HAL_RCC_TIM3_CLK_ENABLE();
    else if (instance == TIM4)  __HAL_RCC_TIM4_CLK_ENABLE();
    else if (instance == TIM5)  __HAL_RCC_TIM5_CLK_ENABLE();
    else if (instance == TIM6)  __HAL_RCC_TIM6_CLK_ENABLE();
    else if (instance == TIM7)  __HAL_RCC_TIM7_CLK_ENABLE();
    else if (instance == TIM8)  __HAL_RCC_TIM8_CLK_ENABLE();
    else if (instance == TIM9)  __HAL_RCC_TIM9_CLK_ENABLE();
    else if (instance == TIM10) __HAL_RCC_TIM10_CLK_ENABLE();
    else if (instance == TIM11) __HAL_RCC_TIM11_CLK_ENABLE();
    else if (instance == TIM12) __HAL_RCC_TIM12_CLK_ENABLE();
    else if (instance == TIM13) __HAL_RCC_TIM13_CLK_ENABLE();
    else                        __HAL_RCC_TIM14_CLK_ENABLE();

    /* ---------- 2. 计算 PSC/ARR ---------- */
    if (_is_apb2_timer(instance))
        timer_clk = HAL_RCC_GetPCLK2Freq() * 2;   /* 168MHz */
    else
        timer_clk = HAL_RCC_GetPCLK1Freq() * 2;   /* 84MHz  */

    psc = timer_clk / 1000000 - 1;
    arr = period_ms * 1000 - 1;

    /* ---------- 3. HAL 基本定时器初始化 ---------- */
    htim.Instance               = instance;
    htim.Init.Prescaler         = psc;
    htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim.Init.Period            = arr;
    htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim) != HAL_OK)
        return -3;

    /* ---------- 4. 设置 NVIC 优先级 ---------- */
    HAL_NVIC_SetPriority(_get_tim_irqn(instance), priority, 0);

    /* ---------- 5. 使能更新中断 ---------- */
    __HAL_TIM_ENABLE_IT(&htim, TIM_IT_UPDATE);

    /* ---------- 6. 启动定时器（中断模式） ---------- */
    if (HAL_TIM_Base_Start_IT(&htim) != HAL_OK)
        return -4;

    me->initialized = true;
    return 0;
}


/**
  * @brief  初始化定时器周期中断（微秒精度）
  * @param  me         定时器对象指针
  * @param  instance   定时器实例
  * @param  period_us  中断周期（微秒），如 250 = 250us
  * @param  callback   用户回调函数
  * @return 同 bsp_tim_init
  */
int bsp_tim_init_us(struct bsp_tim *me, TIM_TypeDef *instance,
                     uint32_t period_us, void (*callback)(void),
                     uint8_t priority)
{
    TIM_HandleTypeDef htim = {0};
    uint32_t timer_clk, psc, arr;

    /* ---------- 参数校验 ---------- */
    if (!me || !instance)
        return -1;

    if (!_is_valid_timer(instance))
        return -1;

    if (period_us == 0)
        return -2;

    /* ---------- 保存配置 ---------- */
    me->instance  = instance;
    me->period_ms = period_us / 1000;   /* 近似值，仅供查阅 */
    me->callback  = callback;

    /* ---------- 1. 使能定时器时钟 ---------- */
    if      (instance == TIM1)  __HAL_RCC_TIM1_CLK_ENABLE();
    else if (instance == TIM2)  __HAL_RCC_TIM2_CLK_ENABLE();
    else if (instance == TIM3)  __HAL_RCC_TIM3_CLK_ENABLE();
    else if (instance == TIM4)  __HAL_RCC_TIM4_CLK_ENABLE();
    else if (instance == TIM5)  __HAL_RCC_TIM5_CLK_ENABLE();
    else if (instance == TIM6)  __HAL_RCC_TIM6_CLK_ENABLE();
    else if (instance == TIM7)  __HAL_RCC_TIM7_CLK_ENABLE();
    else if (instance == TIM8)  __HAL_RCC_TIM8_CLK_ENABLE();
    else if (instance == TIM9)  __HAL_RCC_TIM9_CLK_ENABLE();
    else if (instance == TIM10) __HAL_RCC_TIM10_CLK_ENABLE();
    else if (instance == TIM11) __HAL_RCC_TIM11_CLK_ENABLE();
    else if (instance == TIM12) __HAL_RCC_TIM12_CLK_ENABLE();
    else if (instance == TIM13) __HAL_RCC_TIM13_CLK_ENABLE();
    else                        __HAL_RCC_TIM14_CLK_ENABLE();

    /* ---------- 2. 计算 PSC/ARR ---------- */
    if (_is_apb2_timer(instance))
        timer_clk = HAL_RCC_GetPCLK2Freq() * 2;   /* 168MHz */
    else
        timer_clk = HAL_RCC_GetPCLK1Freq() * 2;   /* 84MHz  */

    psc = timer_clk / 1000000 - 1;          /* tick = 1us */
    arr = period_us - 1;

    /* ---------- 3. HAL 基本定时器初始化 ---------- */
    htim.Instance               = instance;
    htim.Init.Prescaler         = psc;
    htim.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim.Init.Period            = arr;
    htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim) != HAL_OK)
        return -3;

    /* ---------- 4. 设置 NVIC 优先级 ---------- */
    HAL_NVIC_SetPriority(_get_tim_irqn(instance), priority, 0);

    /* ---------- 5. 使能更新中断 ---------- */
    __HAL_TIM_ENABLE_IT(&htim, TIM_IT_UPDATE);

    /* ---------- 6. 启动定时器 ---------- */
    if (HAL_TIM_Base_Start_IT(&htim) != HAL_OK)
        return -4;

    me->initialized = true;
    return 0;
}


/* ============================================================================
 * 反初始化
 * ============================================================================ */

int bsp_tim_deinit(struct bsp_tim *me)
{
    if (!me || !me->initialized)
        return -1;

    /* 直接操作寄存器关定时器 */
    CLEAR_BIT(me->instance->CR1, TIM_CR1_CEN);
    CLEAR_BIT(me->instance->DIER, TIM_DIER_UIE);

    /* 关闭时钟 */
    if      (me->instance == TIM1)  __HAL_RCC_TIM1_CLK_DISABLE();
    else if (me->instance == TIM2)  __HAL_RCC_TIM2_CLK_DISABLE();
    else if (me->instance == TIM3)  __HAL_RCC_TIM3_CLK_DISABLE();
    else if (me->instance == TIM4)  __HAL_RCC_TIM4_CLK_DISABLE();
    else if (me->instance == TIM5)  __HAL_RCC_TIM5_CLK_DISABLE();
    else if (me->instance == TIM6)  __HAL_RCC_TIM6_CLK_DISABLE();
    else if (me->instance == TIM7)  __HAL_RCC_TIM7_CLK_DISABLE();
    else if (me->instance == TIM8)  __HAL_RCC_TIM8_CLK_DISABLE();
    else if (me->instance == TIM9)  __HAL_RCC_TIM9_CLK_DISABLE();
    else if (me->instance == TIM10) __HAL_RCC_TIM10_CLK_DISABLE();
    else if (me->instance == TIM11) __HAL_RCC_TIM11_CLK_DISABLE();
    else if (me->instance == TIM12) __HAL_RCC_TIM12_CLK_DISABLE();
    else if (me->instance == TIM13) __HAL_RCC_TIM13_CLK_DISABLE();
    else                            __HAL_RCC_TIM14_CLK_DISABLE();

    me->initialized = false;
    return 0;
}


/* ============================================================================
 * 中断处理
 * ============================================================================ */

void bsp_tim_irq_handler(struct bsp_tim *me)
{
    if (!me || !me->initialized)
        return;

    /* 清除更新中断标志（UIF）：写 SR 寄存器清标志 */
    me->instance->SR = ~TIM_SR_UIF;

    /* 调用用户回调 */
    if (me->callback)
        me->callback();
}
