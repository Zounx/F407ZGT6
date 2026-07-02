#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "main.h"

/* ============================================================================
 * 调度器初始化
 * ============================================================================ */

/**
 * @brief  调度器初始化
 * @note   配置系统时钟，计算任务表中任务数量，在 main() 初始化阶段调用
 */
void scheduler_init(void);

/**
 * @brief  调度器主循环
 * @note   遍历所有任务，到期则执行，在主循环中轮询调用
 */
void scheduler_run(void);

/**
 * @brief  定时器初始化（注册 TIM 并启动中断）
 * @note   初始化系统定时器（如 TIM6 1ms 心跳），在 scheduler_init 之后调用
 * @return 0=成功，非0=失败
 */
int  scheduler_tim_init(void);

#endif

