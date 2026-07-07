/**
 * @file    fds_data.c
 * @brief   FDS 数据结构层——变量初始化
 *
 * 数据层负责初始化所有全局变量。
 * 类型定义和初始化接口声明在 fds_data.h 中。
 * 全局变量的 extern 声明和定义分别在 fds_param.h / fds_param.c 中。
 */
#include "fds_param.h"
#include <string.h>

/* ============================================================================
 * 初始化
 * ============================================================================ */

void DataInit(void)
{
    memset(&Global_CurrentProgram, 0, sizeof(Global_CurrentProgram));
    memset(&Global_ProgramList,    0, sizeof(Global_ProgramList));
    memset(Global_ProgramStepResultInfos,  0, sizeof(Global_ProgramStepResultInfos));
    memset(&Global_BackdoorParam,  0, sizeof(Global_BackdoorParam));
    memset(&Global_SystemParam,    0, sizeof(Global_SystemParam));
    memset(&Global_OpmaintInfo,    0, sizeof(Global_OpmaintInfo));
    memset(&Global_CurvePointData,  0, sizeof(Global_CurvePointData));
    memset(Global_ProgramInfo,     0, sizeof(Global_ProgramInfo));
    memset(Global_ProgramInfoList, 0, sizeof(Global_ProgramInfoList));

    Global_StepResultCount = 0;
    Global_SystemState      = STATE_IDLE;
    Global_SystemModel      = MODEL_MANUAL;
    Global_LogicLock        = 0;
    Global_ReadyState       = 0;
}
