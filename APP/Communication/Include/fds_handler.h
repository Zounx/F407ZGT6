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

#endif /* FDS_HANDLER_H */
