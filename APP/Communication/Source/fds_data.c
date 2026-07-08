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
    /* 测试初始值 */
    Global_OpmaintInfo.accumulatedRunningTime       = 12345;    // 累计运行时间 12345s
    Global_OpmaintInfo.cumulativeOperationTimes     = 6789;     // 累计操作 6789 次
    Global_OpmaintInfo.suggestedReplaceBelt         = 100000;   // 建议更换同步带
    Global_OpmaintInfo.accumulatedRunningTimeBelt   = 50000;    // 同步带已运行 50000s
    Global_OpmaintInfo.suggestedReplaceLube         = 200000;   // 建议更换润滑油
    Global_OpmaintInfo.accumulatedRunningTimeLube   = 80000;    // 润滑油已运行 80000s
    memset(&Global_CurvePointData,  0, sizeof(Global_CurvePointData));
    memset(Global_ProgramInfo,     0, sizeof(Global_ProgramInfo));
    memset(Global_ProgramInfoList, 0, sizeof(Global_ProgramInfoList));

    Global_StepResultCount = 0;
    Global_SystemState      = STATE_IDLE;
    Global_SystemModel      = MODEL_MANUAL;
    Global_LogicLock        = 0;
    Global_ReadyState       = 0;
}
