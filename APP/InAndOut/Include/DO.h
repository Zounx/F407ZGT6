/**
 * @file    DO.h
 * @brief   数字量输出模块 — 正/负逻辑 + 单脉冲输出
 *
 * 使用方法：
 *   1. 在 DO.c 的 s_cfg[] 中配置每个通道的 GPIO、正/负逻辑
 *   2. main 初始化阶段调用 DO_Init()
 *   3. 用 DO_Set() / DO_Toggle() 控制输出
 *   4. DO_Pulse() 输出单脉冲（需周期调用 DO_Process() 自动关闭）
 *   5. DO_Process() 在调度任务或定时器中调用（处理脉冲定时）
 */

#ifndef IO_DO_H
#define IO_DO_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  初始化所有 DO 通道
 * @note   遍历配置表，为每个通道调用 bsp_gpio_init() 配置为推挽输出
 */
void DO_Init(void);

/**
 * @brief  设置某通道输出电平
 * @param  ch     通道号（0 ~ 总通道数-1）
 * @param  state  true=有效电平，false=无效电平
 * @return 0  成功
 * @return -1 通道号无效
 * @note   正/负逻辑内部处理，调用者只关心有效/无效
 */
int  DO_Set(uint8_t ch, bool state);

/**
 * @brief  读取某通道当前输出状态
 * @param  ch  通道号
 * @retval true   有效电平
 * @retval false  无效电平
 */
bool DO_Get(uint8_t ch);

/**
 * @brief  翻转某通道输出
 * @param  ch  通道号
 * @return 0  成功
 * @return -1 通道号无效
 */
int  DO_Toggle(uint8_t ch);

/**
 * @brief  输出单脉冲
 * @param  ch  通道号
 * @param  ms  脉冲宽度（ms），调用后立即输出有效电平，
 *             经 ms 毫秒后自动恢复为无效电平
 * @note   必须周期调用 DO_Process() 才能自动结束脉冲
 */
void DO_Pulse(uint8_t ch, uint16_t ms);

/**
 * @brief  周期处理（检查脉冲超时并自动关闭）
 * @note   与 DI_Process() 同周期调用即可
 */
void DO_Process(void);

#endif /* IO_DO_H */
