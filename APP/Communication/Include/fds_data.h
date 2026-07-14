/**
 * @file    fds_data.h
 * @brief   FDS 参数表——所有数据结构体定义
 *
 * == 设计思路（参考 PmmBoard 的 system.h）==
 * 本文件只包含数据类型定义（typedef struct / enum / macro）。
 * 全局变量的 extern 声明和定义分别在 fds_param.h / fds_param.c 中。
 *
 * 命名规范：
 *   类型：PascalCase + _T 后缀（如 CurrentProgramInfo_T）
 *   字段：camelCase 匹配 Excel，如 isSaveCuvre
 *   宏/枚举值：大写，如 PROGRAM_NAME_LEN / STATE_IDLE
 */
#ifndef FDS_DATA_H
#define FDS_DATA_H

#include <stdint.h>

/* ============================================================================
 * 常量定义
 * ============================================================================ */

#define PROGRAM_NAME_LEN        17          // 程序名称最大长度（协议定义）
#define PROGRAM_LIST_MAX        128         // 程序列表最大数量
#define CURVE_POINT_MAX         1000        // 曲线数据点最大数量
#define PROGRAM_STEP_RESULT_INFOS_MAX         128         // 步骤结果最大数量

/* ============================================================================
 * 系统状态枚举
 * ============================================================================ */

// 系统运行状态
enum SystemState {
    STATE_IDLE          = 0,    // 空闲
    STATE_RUNNING       = 1,    // 运行中
    STATE_PAUSED        = 2,    // 暂停
    STATE_ERROR         = 3,    // 错误
    STATE_COMPLETE      = 4,    // 完成
};

// 系统模式
enum SystemModel {
    MODEL_MANUAL        = 0,    // 手动模式
    MODEL_AUTO          = 1,    // 自动模式
};

/* ============================================================================
 * 步骤结构体（下位机参数表）
 * ============================================================================ */

// 程序列表信息
typedef struct ProgramListInfo {
    uint8_t  programId[4];                      // 程序 ID（小端序）
    uint8_t  programName[PROGRAM_NAME_LEN];     // 程序名称
} ProgramListInfo_T; 

// 程序列表
typedef struct ProgramListInfos {
    int32_t  currentProgramId;                  // 当前程序 ID
    int32_t  programCount;                      // 程序总数
    ProgramListInfo_T list[PROGRAM_LIST_MAX];   // 程序列表
} ProgramListInfos_T;

// 程序步骤：开始
typedef struct ProgramStepMain {
    int8_t   isSaveCuvre;                       // 是否保存曲线
    int8_t   curveNameType;                     // 曲线名称类型
    int8_t   curveResultType;                   // 曲线结果类型
} ProgramStepMain_T;

// 程序步骤：输入
typedef struct ProgramStepInput {
    int32_t  inputValue;                        // 输入值
} ProgramStepInput_T;

// 程序步骤：寻找插槽
typedef struct ProgramStepFindSlot {
    int32_t  feedStrokeForce1;                  // 进给行程力 1
    int32_t  screwdriverForce1;                 // 螺丝刀力 1
    int32_t  rpm1;                              // 转速 1
    int32_t  projectTorque;                     // 项目扭矩
    int32_t  changoverDepthForce2;              // 切换深度力 2
    int32_t  screwdriverForce2;                 // 螺丝刀力 2
    int32_t  feedStrokeForce2;                  // 进给行程力 2
    int8_t   pressureDecrease;                  // 压力下降
    int32_t  depthMinimum;                      // 深度最小值
    int32_t  depthMaximum;                      // 深度最大值
    int32_t  screwTimeMaximum;                  // 旋入时间最大值
    int32_t  afterrunTorqueControl;             // 运行后扭矩控制
    int8_t   saveResult;                        // 保存结果
} ProgramStepFindSlot_T;

// 程序步骤：输出
typedef struct ProgramStepOutput {
    int32_t  outputValue;                       // 输出值
} ProgramStepOutput_T;

// 程序步骤：形成流孔
typedef struct ProgramStepFormFlowHole {
    int32_t  feedStrokeForce1;                  // 进给行程力 1
    int32_t  screwdriverForce1;                 // 螺丝刀力 1
    int32_t  rpm1;                              // 转速 1
    int32_t  projectTorque;                     // 项目扭矩
    int32_t  changoverDepthForce2;              // 切换深度力 2
    int32_t  screwdriverForce2;                 // 螺丝刀力 2
    int8_t   pressureDecrease;                  // 压力下降
    int32_t  depthSetPoint;                     // 深度设定值
    int32_t  screwTimeMaximum;                  // 旋入时间最大值
    int8_t   saveResult;                        // 保存结果
} ProgramStepFormFlowHole_T;

// 程序步骤：旋入
typedef struct ProgramStepScrewIn {
    int32_t  feedStrokeForce;                   // 进给行程力
    int32_t  screwdriverForce1;                 // 螺丝刀力 1
    int32_t  rpm1;                              // 转速 1
    int32_t  torqueMinimum;                     // 扭矩最小值
    int32_t  torqueMaximum;                     // 扭矩最大值
    int32_t  projectTorque;                     // 项目扭矩
    int32_t  depthMinimum;                      // 深度最小值
    int32_t  depthSetPoint;                     // 深度设定值
    int32_t  depthMaximum;                      // 深度最大值
    int32_t  screwTimeMinimum;                  // 旋入时间最小值
    int32_t  screwTimeMaximum;                  // 旋入时间最大值
    int32_t  afterrunTorqueControl;             // 运行后扭矩控制
    int8_t   saveResult;                        // 保存结果
} ProgramStepScrewIn_T;

// 程序步骤：拧紧
typedef struct ProgramStepFinalTightening {
    int32_t  feedStrokeForce;                   // 进给行程力
    int32_t  screwdriverForce1;                 // 螺丝刀力 1
    int32_t  rpm1;                              // 转速 1
    int8_t   dynAdaptation;                     // 动态适应
    int32_t  torqueMinimum;                     // 扭矩最小值
    int32_t  torqueSetPoint;                    // 扭矩设定值
    int32_t  torqueMaximum;                     // 扭矩最大值
    int32_t  torqueServo;                       // 扭矩-伺服
    int32_t  torqueThreshold;                   // 扭矩阈值
    int32_t  angleMinimum;                      // 角度最小值
    int32_t  angleMaximum;                      // 角度最大值
    int32_t  depthMinimum;                      // 深度最小值
    int32_t  depthMaximum;                      // 深度最大值
    int32_t  screwTimeMinimum;                  // 旋入时间最小值
    int32_t  screwTimeMaximum;                  // 旋入时间最大值
    int32_t  afterrunTorqueControl;             // 运行后扭矩控制
    int8_t   saveResult;                        // 保存结果
} ProgramStepFinalTightening_T;

// 程序步骤：返回螺丝刀行程
typedef struct ProgramStepReturnScrewdriverStroke {
    int8_t   waitUntilRetracted;                // 等待回缩
    int32_t  counterForce;                      // 反作用力
} ProgramStepReturnScrewdriverStroke_T;

// 程序步骤：回缩钳口行程
typedef struct ProgramStepRetractJawStroke {
    int8_t   waitUntilRetracted;                // 等待回缩
    int32_t  counterForce;                      // 反作用力
} ProgramStepRetractJawStroke_T;

// 程序步骤：跳转
typedef struct ProgramStepJump {
    int8_t   jumpCondition;                     // 跳转条件
    int16_t  jumpToStep;                        // 跳转目标步骤
    int8_t   jumpToCount;                       // 跳转计数
} ProgramStepJump_T;

// 程序步骤数据联合体（同一时刻只有一个步骤类型激活）
typedef union {
    ProgramStepMain_T                     psMain;               // 开始步骤
    ProgramStepInput_T                    psInput;              // 输入步骤
    ProgramStepFindSlot_T                 psFindSlot;           // 寻找插槽步骤
    ProgramStepOutput_T                   psOutput;             // 输出步骤
    ProgramStepFormFlowHole_T             psFormFlowHole;       // 形成流孔步骤
    ProgramStepScrewIn_T                  psScrewIn;            // 旋入步骤
    ProgramStepFinalTightening_T          psFinalTightening;    // 拧紧步骤
    ProgramStepReturnScrewdriverStroke_T  psReturnScrewdriverStroke;  // 返回螺丝刀行程步骤
    ProgramStepRetractJawStroke_T         psRetractJawStroke;         // 回缩钳口行程步骤
    ProgramStepJump_T                     psJump;               // 跳转步骤
} ProgramStepData_U;

// 当前程序信息
typedef struct CurrentProgramInfo {
    int16_t           currentStep;                       // 当前步骤
    int16_t           stepType;                          // 步骤类型
    int16_t           stepState;                         // 步骤状态
    int16_t           stepFlag;                          // 步骤标志
    ProgramStepData_U stepData;                          // 步骤数据（联合体）
} CurrentProgramInfo_T;

// 程序执行步骤结果信息
typedef struct ProgramStepResultInfos {
    int32_t  stepIndex;                          // 程序步骤索引
    int32_t  stepType;                           // 程序步骤类型
    int32_t  torque;                             // 扭矩
    int32_t  depth;                              // 深度
    int32_t  angle;                              // 角度
    int32_t  force;                              // 压力
    int32_t  result;                             // 结果
} ProgramStepResultInfos_T;

// 曲线数据点
typedef struct CurvePointData {
    int32_t  depth;                              // 深度
    int32_t  torqueMotor;                        // 扭矩-电机
    int32_t  torqueSensor;                       // 扭矩-传感器
    int32_t  voltageSensor;                      // 电压-传感器
    int32_t  angle;                              // 角度
    int32_t  force;                              // 压力
    int32_t  rpm;                                // 转速
    int32_t  step;                               // 步骤
    int32_t  time;                               // 时间
    int32_t  count;                              // 点数
    int32_t  voltageReserved;                    // 电压（预留）
} CurvePointData_T;

/* ============================================================================
 * 协议 body 结构体（通讯协议数据载荷）
 * ============================================================================ */

// 心跳
typedef struct Heartbeat {
    int32_t  tickMs;                             // 心跳发送时刻（HAL_GetTick）
} Heartbeat_T;

// 通用单结果
typedef struct Result {
    int32_t  result;                             // 0=失败, 1=成功
} Result_T;

// 后门参数
typedef struct BackdoorParam {
    uint8_t  seriesModel[50];                    // 系列型号
    uint8_t  serialNumber[50];                   // 序列号
    uint8_t  firmwareVersion[50];                // 固件版本
    uint8_t  motherboardVersion[50];             // 主板版本
    int32_t  suggestedReplaceBelt;               // 建议更换同步带计数
    int32_t  suggestedReplaceLube;               // 建议更换润滑油计数
} BackdoorParam_T;

// 系统参数
typedef struct SystemParam {
    int32_t  positionUnit;                       // 位置单位
    int32_t  positionFormat;                     // 位置格式
    int32_t  pressureUnit;                       // 压力单位
    int32_t  pressureFormat;                     // 压力格式
    int32_t  torqueUnit;                         // 扭矩单位
    int32_t  torqueFormat;                       // 扭矩格式
    int32_t  languageType;                       // 语言类型
    int32_t  curvePushFrequency;                 // 曲线推送频率
} SystemParam_T;

// 运维信息
typedef struct OpmaintInfo {
    int32_t  accumulatedRunningTime;             // 累计运行时间
    int32_t  cumulativeOperationTimes;           // 累计操作次数
    int32_t  suggestedReplaceBelt;               // 建议更换同步带计数
    int32_t  accumulatedRunningTimeBelt;         // 同步带累计运行时间
    int32_t  suggestedReplaceLube;               // 建议更换润滑油计数
    int32_t  accumulatedRunningTimeLube;         // 润滑油累计运行时间
} OpmaintInfo_T;

// 运维设置
typedef struct OpmaintSet {
    int32_t  dataType;                           // 1=清除同步带, 2=清除润滑油
} OpmaintSet_T;

// 系统状态推送
typedef struct SysStatePush {
    int32_t  logicLock;                          // 逻辑锁
    int32_t  systemModel;                        // 系统模式
    int32_t  readyState;                         // 就绪状态
    int32_t  systemState;                        // 系统状态
} SysStatePush_T;

// 报警信息
typedef struct SysError {
    int32_t  errorType;                          // 1=警告, 2=错误
    int32_t  errorCode;                          // 错误码
} SysError_T;

// IO 状态
typedef struct IoState {
    int32_t  ioInputState;                       // IO 输入状态
    int32_t  ioOutputState;                      // IO 输出状态
    int32_t  plcInputState;                      // PLC 输入状态
    int32_t  plcOutputState;                     // PLC 输出状态
    int32_t  stepInputValue;                     // 步骤输入值
    int32_t  stepOutputValue;                    // 步骤输出值
    int32_t  programId;                          // 程序 ID
} IoState_T;

// 日期时间
typedef struct Datetime {
    int32_t  timeYear;                           // 年
    int32_t  timeMonth;                          // 月
    int32_t  timeDay;                            // 日
    int32_t  timeHour;                           // 时
    int32_t  timeMinute;                         // 分
    int32_t  timeSecond;                         // 秒
} Datetime_T;

// 曲线获取请求
typedef struct GetCurve {
    int32_t  pointIndex;                         // 曲线点索引
} GetCurve_T;

// 伺服旋转
typedef struct ServoTurn {
    int32_t  directionType;                      // 1=顺时针, 2=逆时针
    int32_t  turnType;                           // 1=速度, 2=角度
    int32_t  rpmSpeed;                           // 转速
    int32_t  servoTorque;                        // 伺服扭矩
    int32_t  servoAngle;                         // 伺服角度
} ServoTurn_T;

// 气缸控制
typedef struct CylinderCtrl {
    int32_t  directionType;                      // 1=向上, 2=向下
    int32_t  counterForce;                       // 反作用力
} CylinderCtrl_T;

// 订阅
typedef struct Subscribe {
    int32_t  subType;                            // 1=订阅, 2=取消订阅
} Subscribe_T;

// 实时数值推送
typedef struct CurrentValues {
    int32_t  servoRpmSpeed;                      // 伺服转速
    int32_t  servoTorque;                        // 伺服扭矩
    int32_t  servoAngle;                         // 伺服角度
    int32_t  cylinderPosition;                   // 气缸位置
} CurrentValues_T;

/* ============================================================================
 * 初始化接口
 * ============================================================================ */

/**
 * @brief  初始化数据层，将所有全局变量清零
 */
void DataInit(void);

#endif /* FDS_DATA_H */
