/**
 * @file    fds_param.h
 * @brief   FDS 参数表——全局变量声明
 *
 * 包含所有全局变量的 extern 声明：
 *   1. 参数表定义（struct 实例）
 *   2. 数组定义（程序存储区）
 *   3. 运行时全局变量
 *
 * 变量定义在 fds_param.c 中，类型定义在 fds_data.h 中。
 */
#ifndef FDS_PARAM_H
#define FDS_PARAM_H

#include <stdint.h>
#include "fds_data.h"

/* ============================================================================
 * 参数表定义（extern）
 * ============================================================================ */

extern CurrentProgramInfo_T  Global_CurrentProgram;     // 当前程序信息
extern ProgramListInfos_T    Global_ProgramList;        // 程序列表
extern ProgramStepResultInfos_T  Global_ProgramStepResultInfos[PROGRAM_STEP_RESULT_INFOS_MAX]; // 步骤结果数组
extern BackdoorParam_T       Global_BackdoorParam;      // 后门参数
extern SystemParam_T         Global_SystemParam;        // 系统参数
extern OpmaintInfo_T         Global_OpmaintInfo;        // 运维信息
extern CurvePointData_T      Global_CurvePointData;    // 曲线数据点
extern Datetime_T      			 Global_DateTime;          // 当前系统时间

/* ============================================================================
 * 数组定义（程序存储区，对应 Excel "数组定义" sheet）
 * ============================================================================ */

extern uint8_t  Global_ProgramInfo[5000];         // 程序存储区（字节数组）
extern uint8_t  Global_ProgramInfoList[2688];     // 程序列表存储区（字节数组）

/* ============================================================================
 * 运行时全局变量
 * ============================================================================ */

extern int32_t  Global_StepResultCount;            // 步骤结果数量
extern int32_t  Global_SystemState;                // 系统状态
extern int32_t  Global_SystemModel;                // 系统模式
extern int32_t  Global_LogicLock;                  // 逻辑锁
extern int32_t  Global_ReadyState;                 // 准备信号


#endif /* FDS_PARAM_H */
