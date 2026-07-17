/**
 * @file    fds_handler.h
 * @brief   FDS 业务处理层——指令分发和业务逻辑接口
 *
 * 处理所有上位机发来的指令（functionId 112~155），
 * 封装对 fds_data 和 fds_protocol 的调用。
 */
#ifndef FDS_HANDLER_H
#define FDS_HANDLER_H

#include <stdint.h>
#include "fds_data.h"

/* USART1 对象（由 Main.c 定义） */
extern struct bsp_usart s_usart1;

/* ============================================================================
 * 调试开关
 *
 * ETH_FDS_DEBUG 设为 1 时 FDS_DEBUG() 通过 USART1 打印调试信息；
 * 设为 0 时所有 FDS_DEBUG 被编译为空，不产生任何代码。
 * ============================================================================ */

#define ETH_FDS_DEBUG  1       /* 调试开关：1=开启，0=关闭 */

#if ETH_FDS_DEBUG
    #define FDS_DEBUG(...)  bsp_usart_printf(&s_usart1, __VA_ARGS__)
#else
    #define FDS_DEBUG(...)  ((void)0)
#endif

/**
 * @brief  初始化 FDS 业务层
 * @note   初始化数据层、注册分发表、清空状态
 */
void HandlerInit(void);

/**
 * @brief  周期任务（由 scheduler 调度）
 * @note   轮询接收 WIZnet 数据，解析并分发
 */
void HandlerTask(void);

/**
 * @brief  推送系统状态（FID=125）
 * @note   系统状态变化时调用，自动发送当前 logicLock/systemModel/readyState/systemState
 */
void PushSystemState(void);

/**
 * @brief  推送报警信息（FID=127）
 * @param  errorType  1=警告, 2=错误
 * @param  errorCode  错误码
 */
void PushSystemError(int32_t errorType, int32_t errorCode);

/**
 * @brief  推送程序执行步骤结果（FID=135）
 * @note   发送 Global_ProgramStepResultInfos 全量数组（128 条 × 28 字节）
 */
void PushStepResult(void);

/**
 * @brief  推送心跳（FID=110）
 * @note   注册到 scheduler，1000ms 周期由定时器驱动
 */
void PushHeartbeat(void);

#endif /* FDS_HANDLER_H */
