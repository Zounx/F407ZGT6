/**
 * @file    fds_param.c
 * @brief   FDS 参数表——全局变量定义
 *
 * 组织方式：
 *   1. 参数表定义段：持久化状态变量（对应下位机参数表）
 *   2. 数组定义段：程序存储区
 *   3. 全局变量段：非持久化运行时状态变量
 */
#include "fds_param.h"

/* ============================================================================
 * 参数表定义
 * ============================================================================ */

CurrentProgramInfo_T  Global_CurrentProgram;          // 当前程序信息
ProgramListInfos_T    Global_ProgramList;             // 程序列表
ProgramStepResultInfos_T  Global_ProgramStepResultInfos[PROGRAM_STEP_RESULT_INFOS_MAX]; // 步骤结果数组
BackdoorParam_T       Global_BackdoorParam;           // 后门参数
SystemParam_T         Global_SystemParam;             // 系统参数
OpmaintInfo_T         Global_OpmaintInfo;           // 运维信息
CurvePointData_T      Global_CurvePointData;         // 曲线数据点
Datetime_T    				Global_DateTime;               // 当前系统时间

/* ============================================================================
 * 数组定义（程序存储区）
 * ============================================================================ */

uint8_t  Global_ProgramInfo[5000];                        // 程序存储区
uint8_t  Global_ProgramInfoList[2688];                    // 程序列表存储区

/* ============================================================================
 * 运行时全局变量
 * ============================================================================ */

int32_t  Global_StepResultCount;                          // 步骤结果数量
int32_t  Global_SystemState;                              // 系统状态
int32_t  Global_SystemModel;                              // 系统模式
int32_t  Global_LogicLock;                                // 逻辑锁
int32_t  Global_ReadyState;                               // 准备信号


