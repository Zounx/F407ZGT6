/**
 * @file    bsp_tim.h
 * @brief   定时器周期中断驱动模块（F407 版，支持 TIM1~14）
 *
 * ============================================================================
 *  快速开始
 * ============================================================================
 *
 * // 1. 定义定时器对象
 * static struct bsp_tim  s_tim6;      // 1ms
 * static struct bsp_tim  s_tim7;      // 10ms
 *
 * // 2. 回调函数
 * static void tim6_cb(void) { bsp_usart_proc(&s_usart1); }
 * static void tim7_cb(void) { bsp_gpio_toggle(&s_led); }
 *
 * // 3. 初始化
 * bsp_tim_init(&s_tim6, TIM6, 1,   tim6_cb);   // 1ms
 * bsp_tim_init(&s_tim7, TIM7, 10,  tim7_cb);   // 10ms
 *
 * // 4. 中断函数
 * void TIM6_DAC_IRQHandler(void) { bsp_tim_irq_handler(&s_tim6); }
 * void TIM7_IRQHandler(void)      { bsp_tim_irq_handler(&s_tim7); }
 *
 * ============================================================================
 *  支持哪些定时器（F407 共 14 个）
 * ============================================================================
 *
 *   高级控制定时器（APB2，168MHz，PSC=167，16位）：
 *     TIM1, TIM8
 *
 *   通用定时器（APB1 84MHz PSC=83 / APB2 168MHz PSC=167）：
 *     TIM2      - 32位通用（APB1）
 *     TIM3/4    - 16位通用（APB1）
 *     TIM5      - 32位通用（APB1）
 *     TIM9~11   - 16位通用（APB2）
 *     TIM12~14  - 16位通用（APB1）
 *
 *   基本定时器（APB1，84MHz，PSC=83，16位）：
 *     TIM6, TIM7
 *
 *   时钟源（F407 @168MHz）：
 *     APB1 预分频 = 4（>1），APB1 定时器时钟 = 2 x PCLK1 = 84MHz
 *     APB2 预分频 = 2（>1），APB2 定时器时钟 = 2 x PCLK2 = 168MHz
 *     APB1 定时器：PSC = 84M / 1M - 1 = 83 => tick = 1us
 *     APB2 定时器：PSC = 168M / 1M - 1 = 167 => tick = 1us
 *     ARR = period_ms * 1000 - 1
 *
 *   最大周期（tick=1us 时）：
 *     TIM2/5 (32位) : ARR 最大 0xFFFFFFFF => 约 4294 秒
 *     其余 (16位) : ARR 最大 0xFFFF => 约 65 毫秒
 *     16 位定时器需要更长周期时，自行加大 PSC（牺牲精度换取周期）
 *
 * ============================================================================
 *  共享中断向量说明
 * ============================================================================
 *
 *  以下定时器对共用同一个中断向量，使用时在同一个 IRQHandler
 *  中依次调用 bsp_tim_irq_handler（函数内部会跳过未初始化的对象）：
 *
 *     TIM1_BRK_TIM9_IRQHandler      ← TIM1 Break + TIM9
 *     TIM1_UP_TIM10_IRQHandler      ← TIM1 Update + TIM10
 *     TIM1_TRG_COM_TIM11_IRQHandler ← TIM1 Trigger/Commutation + TIM11
 *     TIM8_BRK_TIM12_IRQHandler     ← TIM8 Break + TIM12
 *     TIM8_UP_TIM13_IRQHandler      ← TIM8 Update + TIM13
 *     TIM8_TRG_COM_TIM14_IRQHandler ← TIM8 Trigger/Commutation + TIM14
 */

#ifndef BSP_TIM_H
#define BSP_TIM_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>


/* ============================================================================
 * 内部类型（私有字段，用户勿直接访问成员）
 * ============================================================================ */

/**
 * 定时器对象
 *
 * 每个定时器实例定义一个此结构体变量。
 * 所有操作通过函数完成，不要直接读写字段。
 */
struct bsp_tim {
    TIM_TypeDef    *instance;        /**< 定时器基址，如 TIM2 / TIM6 / TIM7 */
    uint32_t        period_ms;       /**< 中断周期（毫秒） */
    void          (*callback)(void); /**< 用户回调函数（中断中执行） */
    bool            initialized;     /**< init 成功后为 true */
};


/* ============================================================================
 * API 函数
 * ============================================================================ */

/**
 * @brief  初始化定时器周期中断
 *
 * 完成以下工作：
 *   1. 使能定时器时钟（自动识别 APB1/APB2 定时器）
 *   2. 根据时钟频率 + period_ms 自动计算 PSC/ARR
 *   3. 调用 HAL_TIM_Base_Init 配置
 *   4. 使能更新中断（UPDATE IT）
 *   5. 设置 NVIC 中断优先级（抢占优先级，NVIC_PriorityGroup_4）
 *   6. 调用 HAL_TIM_Base_Start_IT 启动
 *
 * @param me         定时器对象指针
 * @param instance   定时器实例：TIM1~TIM14（APB1/APB2 自动识别）
 * @param period_ms  中断周期（毫秒），例如 1、10、100、1000
 * @param callback   用户回调函数（中断中调用，可传 NULL）
 * @param priority  NVIC 抢占优先级（0~15，0=最高，15=最低）
 *                  建议周期越短优先级越高，如 250us→0、1ms→1、100ms→3
 *
 * @return 0  成功
 * @return -1 me/instance 为 NULL 或不支持的定时器
 * @return -2 period_ms 为 0
 * @return -3 HAL_TIM_Base_Init 失败
 * @return -4 HAL_TIM_Base_Start_IT 失败
 *
 * @note 中断向量需在 it.c 或 scheduler.c 中定义，例如：
 *       void TIM2_IRQHandler(void) { bsp_tim_irq_handler(&s_tim2); }
 * @note callback 在中断上下文中执行，应尽量短，不要在其中等待或轮询
 * @note 16位定时器在 tick=1us 时最大周期约 65ms，
 *       需要更长时间请修改 PSC 计算逻辑（牺牲精度换取更长周期）
 */
int  bsp_tim_init(struct bsp_tim *me, TIM_TypeDef *instance,
                  uint32_t period_ms, void (*callback)(void),
                  uint8_t priority);

/**
 * @brief  初始化定时器周期中断（微秒精度）
 *
 * 与 bsp_tim_init 的区别：period_us 单位是微秒，适用 <1ms 的高频周期。
 *
 * @param me         定时器对象指针
 * @param instance   定时器实例
 * @param period_us  中断周期（微秒），例如 250 = 250μs
 * @param callback   用户回调函数
 * @param priority  NVIC 抢占优先级（0~15）
 * @return 同 bsp_tim_init
 */
int  bsp_tim_init_us(struct bsp_tim *me, TIM_TypeDef *instance,
                     uint32_t period_us, void (*callback)(void),
                     uint8_t priority);

/**
 * @brief  反初始化定时器
 *
 * 停止定时器计数、关闭更新中断、关闭时钟。
 *
 * @param me  定时器对象指针
 * @return 0  成功
 * @return -1 me 为 NULL 或未 init
 */
int  bsp_tim_deinit(struct bsp_tim *me);

/**
 * @brief  定时器中断处理函数
 *
 * 在对应的 IRQHandler 中调用：
 *   void TIM2_IRQHandler(void) { bsp_tim_irq_handler(&s_tim2); }
 *
 * 内部完成：
 *   1. 清除更新中断标志（UIF）
 *   2. 调用用户注册的 callback
 *
 * @param me  定时器对象指针
 */
void bsp_tim_irq_handler(struct bsp_tim *me);


#endif /* BSP_TIM_H */
