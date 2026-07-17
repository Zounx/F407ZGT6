/**
 * @file    DI.h
 * @brief   数字量输入模块 — 完整流水线：正/负逻辑 → 滤波 → ON/OFF独立延时
 *
 * 流水线：
 *   Raw → LogicDeal(正/负逻辑) → Filtr(等长防抖) → Delay(ON/OFF独立延时) → Out
 *
 * 使用方法：
 *   1. 在 DI.c 的 s_cfg[] 中配置每个通道的参数
 *   2. main 初始化阶段调用 DI_Init()
 *   3. 在 1ms 定时器或固定周期调度任务中调用 DI_Process()
 *   4. DI_GetOut(ch) 读取最终输出
 *
 * 时间单位说明：
 *   filtr_time / en_delay_time / dis_delay_time 的单位为"处理周期数"。
 *   假设 DI_Process 以 10ms 周期调用，filtr_time=5 表示 5×10ms=50ms 滤波。
 */

#ifndef IO_DI_H
#define IO_DI_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  初始化所有 DI 通道（配置 GPIO 为输入，清空状态）
 */
void DI_Init(void);

/**
 * @brief  周期处理 — 每调用一次完成所有通道的完整流水线
 * @note   在固定周期的定时器或调度任务中调用，
 *         周期决定了 filtr_time / delay_time 的实际时间跨度
 */
void DI_Process(void);

/**
 * @brief  获取某通道的最终输出状态（滤波+延时后的最终结果）
 * @param  ch  通道号（0 ~ 总通道数-1）
 * @retval 1   有效
 * @retval 0   无效
 */
uint16_t DI_GetOut(uint8_t ch);

/**
 * @brief  获取某通道的原始 GPIO 电平（未滤波、未取反、未延时）
 * @param  ch  通道号（0 ~ 总通道数-1）
 * @retval 1   高电平
 * @retval 0   低电平
 */
uint16_t DI_GetRaw(uint8_t ch);

#endif /* IO_DI_H */
