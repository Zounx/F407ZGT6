# FDS 通信协议参考手册

> 本文档面向 **上位机开发人员**，说明 FDS 协议的报文格式和各功能码的请求/响应示例。

---

## 一、通用报文格式

所有报文遵循统一的 **16 字节头部 + 变长 body** 结构：

```
| packetLength(4) | functionId(4) | requestId(4) | bodyLength(4) | body... |
|-----------------|---------------|--------------|---------------|---------|
        ↑              ↑              ↑               ↑           ↑
    包总长度          功能码        请求ID(原样返回)   body长度    数据载荷
```

- 字节序：**小端**（与 STM32/Intel CPU 一致）
- `packetLength` = 16 + `bodyLength`
- `requestId`：上位机生成，下位机在响应中**原样返回**，用于配对请求与响应

---

## 二、心跳（FID=110→110）

下位机 **主动推送**，每秒一次。

### 请求（无，由设备主动发送）

| 方向 | 报文 |
|------|------|
| 设备→PC | `14 00 00 00 6E 00 00 00 00 00 00 00 04 00 00 00 [tick]` |

- `6E` = 110 = Heartbeat 心跳
- `requestId` = 0（推送消息无请求 ID）
- body = 1 个 int32_t：当前系统 tick 值（ms），用于计算通信延时

### 示例

```
14 00 00 00 6E 00 00 00 00 00 00 00 04 00 00 00 06 19 00 00
                                                    ↑ tick=6406ms
```

---

## 三、后门参数

### 定义

```c
typedef struct BackdoorParam {
    uint8_t  seriesModel[50];         // 系列型号      偏移 0
    uint8_t  serialNumber[50];        // 序列号        偏移 50
    uint8_t  firmwareVersion[50];     // 固件版本      偏移 100
    uint8_t  motherboardVersion[50];  // 主板版本      偏移 150
    int32_t  suggestedReplaceBelt;    // 建议更换同步带  偏移 200
    int32_t  suggestedReplaceLube;    // 建议更换润滑油  偏移 204
} BackdoorParam_T;  // 总大小 = 208 字节
```

### 设置（FID=112→113）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 112 | 请求设置后门参数（body = BackdoorParam_T） |
| 设备→PC | 113 | 设置结果（body = 1 个 int32_t，1=成功） |

**请求示例**（PC 发送）：
```
E0 00 00 00 70 00 00 00 01 00 00 00 D0 00 00 00  ← 头部
54 31 31 30 2D 50 72 6F 00 ...  ← seriesModel = "T110-Pro"
53 4E 32 30 32 36 30 36 30 31  ...  ← serialNumber = "SN20260601"
56 31 2E 30 2E 30 ...  ← firmwareVersion = "V1.0.0"
4D 42 2D 56 32 2E 31 ...  ← motherboardVersion = "MB-V2.1"
A0 86 01 00  ← suggestedReplaceBelt = 100000
50 C3 00 00  ← suggestedReplaceLube = 50000
```
- `E0` = 224 = packetLength(16+208)
- `70` = 112 = ReqSetBackDoorParameter 后门参数设置请求
- `D0` = 208 = bodyLength

**响应示例**（设备返回）：
```
14 00 00 00 71 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `71` = 113 = RspSetBackDoorParameter 后门参数设置响应
- body = `01 00 00 00` = result=1（成功）

### 获取（FID=114→115）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 114 | 请求获取后门参数（body 为空） |
| 设备→PC | 115 | 返回 BackdoorParam_T（208 字节） |

**请求示例**：
```
10 00 00 00 72 00 00 00 02 00 00 00 00 00 00 00
```
- `10` = 16 = packetLength（仅头部，无 body）
- `72` = 114 = ReqGetBackDoorParameter 后门参数获取请求

**响应示例**：
```
E0 00 00 00 73 00 00 00 02 00 00 00 D0 00 00 00  ← 头部
54 31 31 30 2D 50 72 6F ...  ← 208 字节 body，格式同 BackdoorParam_T
```
- `73` = 115 = RspGetBackDoorParameter 后门参数获取响应
- `requestId` = `02`（与请求的 requestId 一致）

---

## 四、系统参数

### 定义

```c
typedef struct SystemParam {
    int32_t  positionUnit;           // 位置单位          偏移 0
    int32_t  positionFormat;         // 位置格式          偏移 4
    int32_t  pressureUnit;           // 压力单位          偏移 8
    int32_t  pressureFormat;         // 压力格式          偏移 12
    int32_t  torqueUnit;             // 扭矩单位          偏移 16
    int32_t  torqueFormat;           // 扭矩格式          偏移 20
    int32_t  languageType;           // 语言类型          偏移 24
    int32_t  curvePushFrequency;     // 曲线推送频率       偏移 28
} SystemParam_T;  // 总大小 = 32 字节
```

### 设置（FID=116→117）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 116 | 请求设置系统参数（body = SystemParam_T） |
| 设备→PC | 117 | 设置结果 |

**请求示例**：
```
30 00 00 00 74 00 00 00 01 00 00 00 20 00 00 00
00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
```
- `74` = 116 = ReqSetSystemParameter 系统参数设置请求
- body = 32 字节（8 个 int32_t），示例全 0

**响应示例**：
```
14 00 00 00 75 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `75` = 117 = RspSetSystemParameter 系统参数设置响应

### 获取（FID=118→119）

**请求示例**：
```
10 00 00 00 76 00 00 00 01 00 00 00 00 00 00 00
```
- `76` = 118 = ReqGetSystemParameter 系统参数获取请求

**响应示例**：
```
30 00 00 00 77 00 00 00 01 00 00 00 20 00 00 00
39 30 00 00 85 1A 00 00 A0 86 01 00 50 C3 00 00
40 0D 03 00 80 38 01 00 00 00 00 00 00 00 00 00
```
- `77` = 119 = RspGetSystemParameter 系统参数获取响应

---

## 五、运维信息

### 定义

```c
typedef struct OpmaintInfo {
    int32_t  accumulatedRunningTime;      // 累计运行时间          偏移 0
    int32_t  cumulativeOperationTimes;    // 累计操作次数          偏移 4
    int32_t  suggestedReplaceBelt;        // 建议更换同步带计数    偏移 8
    int32_t  accumulatedRunningTimeBelt;  // 同步带累计运行时间    偏移 12
    int32_t  suggestedReplaceLube;        // 建议更换润滑油计数    偏移 16
    int32_t  accumulatedRunningTimeLube;  // 润滑油累计运行时间    偏移 20
} OpmaintInfo_T;  // 总大小 = 24 字节

typedef struct OpmaintSet {
    int32_t  dataType;  // 1=清除同步带, 2=清除润滑油
} OpmaintSet_T;  // 总大小 = 4 字节
```

### 获取（FID=120→121）

**请求示例**：
```
10 00 00 00 78 00 00 00 01 00 00 00 00 00 00 00
```
- `78` = 120 = ReqGetOpmaintInfo 运维信息获取请求

**响应示例**：
```
28 00 00 00 79 00 00 00 01 00 00 00 18 00 00 00
39 30 00 00  85 1A 00 00  A0 86 01 00  50 C3 00 00
40 0D 03 00  80 38 01 00
```
- `79` = 121 = RspGetOpmaintInfo 运维信息获取响应
- body = 24 字节（6 个 int32_t）

### 清除计数（FID=122→123）

**请求示例**（清除同步带 dataType=1）：
```
14 00 00 00 7A 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `7A` = 122 = ReqSetOpmaintInfo 运维信息清除请求
- body = `01 00 00 00` = dataType=1

**请求示例**（清除润滑油 dataType=2）：
```
14 00 00 00 7A 00 00 00 01 00 00 00 04 00 00 00 02 00 00 00
```

**响应示例**（通用）：
```
14 00 00 00 7B 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `7B` = 123 = RspSetOpmaintInfo 运维信息清除响应

---

## 六、主动推送（设备→PC，FID=125/127/135）

### 系统状态推送（FID=125）

设备在系统状态变化时主动推送，body 结构：

```c
typedef struct SysStatePush {
    int32_t  logicLock;     // 逻辑锁（0=关, 1=开）
    int32_t  systemModel;   // 系统模式（0=手动, 1=自动）
    int32_t  readyState;    // 就绪状态
    int32_t  systemState;   // 系统状态（0=空闲, 1=运行, 2=暂停, 3=错误, 4=完成）
} SysStatePush_T;  // 总大小 = 16 字节
```

**报文示例**：
```
28 00 00 00 7D 00 00 00 00 00 00 00 10 00 00 00  ← 头部
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ← body（全 0）
```

### 报警推送（FID=127）

```c
typedef struct SysError {
    int32_t  errorType;   // 1=警告, 2=错误
    int32_t  errorCode;   // 错误码
} SysError_T;  // 总大小 = 8 字节
```

### 程序执行步骤结果推送（FID=135）

程序每执行完一个步骤，设备主动推送步骤结果信息。

```c
typedef struct ProgramStepResultInfos {
    int32_t  stepIndex;    // 程序步骤索引
    int32_t  stepType;     // 程序步骤类型
    int32_t  torque;       // 扭矩
    int32_t  depth;        // 深度
    int32_t  angle;        // 角度
    int32_t  force;        // 压力
    int32_t  result;       // 结果
} ProgramStepResultInfos_T;  // 总大小 = 28 字节
```

body 为 128 条记录的数组（每个步骤一条，共 896 个 int32_t = 3584 字节）：

```
| stepIndex(4) | stepType(4) | torque(4) | depth(4) | angle(4) | force(4) | result(4) |  ← 第1步
| stepIndex(4) | stepType(4) | torque(4) | depth(4) | angle(4) | force(4) | result(4) |  ← 第2步
...
| ... | ... | ... | ... | ... | ... | ... |  ← 第128步
```

**报文示例**：
```
10 0E 00 00 87 00 00 00 00 00 00 00 00 0E 00 00
[3584 bytes body：128 条 × 28 字节]
```
- `87` = 135 = SendProgramStepResultInfos 步骤结果推送
- `00 0E 00 00` = 3584 = bodyLength
- `10 0E` = 16+3584 = 3600 = packetLength

---

## 七、保存程序（FID=128→129）

### 定义

```c
typedef struct CurrentProgramInfo {
    int16_t           currentStep;        // 当前步骤
    int16_t           stepType;           // 步骤类型
    int16_t           stepState;          // 步骤状态
    int16_t           stepFlag;           // 步骤标志
    ProgramStepData_U stepData;           // 步骤数据（联合体，根据 stepType 解释）
} CurrentProgramInfo_T;  // 具体大小见 sizeof()
```

### 请求/响应

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 128 | 保存当前程序（body = CurrentProgramInfo_T） |
| 设备→PC | 129 | 保存结果 |

**请求示例**（仅发 4 字节测试 body）：
```
14 00 00 00 80 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `80` = 128 = ReqSaveProgram 程序保存请求
- body = `01 00 00 00` = currentStep=1

**响应示例**：
```
14 00 00 00 81 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `81` = 129 = RspSaveProgram 程序保存响应
- body = `01 00 00 00` = result=1（成功）

---

## 八、程序列表信息获取（FID=130→131）

### 定义

```c
typedef struct ProgramListInfo {
    int32_t  programId;                         // 程序 ID
    uint8_t  programName[PROGRAM_NAME_LEN];     // 程序名称（17 字节）
} ProgramListInfo_T;  // 21 字节

typedef struct ProgramListInfos {
    int32_t  currentProgramId;                  // 当前程序 ID
    int32_t  programCount;                      // 程序总数
    ProgramListInfo_T list[PROGRAM_LIST_MAX];   // 程序列表（128 条）
} ProgramListInfos_T;  // 总大小见下文
```

**注意**：下位机以字节数组 `Global_ProgramInfoList[2688]` 承载列表数据（128 条 × 21 字节），
结构体定义供参考，实际 wire format 与结构体可能存在对齐差异。

### 请求/响应

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 130 | 请求程序列表（body 为空） |
| 设备→PC | 131 | 返回程序列表（body = 2688 字节数组） |

**请求示例**：
```
10 00 00 00 82 00 00 00 01 00 00 00 00 00 00 00
```
- `82` = 130 = ReqGetProgramListInfos 程序列表获取请求
- body 为空，packetLength = 16

**响应示例**：
```
A0 0A 00 00 83 00 00 00 01 00 00 00 80 0A 00 00
01 00 00 00  02 00 00 00  ← currentProgramId=1, programCount=2
01 00 00 00  54 31 31 30 2D 50 72 6F 00 00 00 00 00 00 00 00 00 00  ← ID=1, Name="T110-Pro"
02 00 00 00  54 65 73 74 50 72 6F 67 00 00 00 00 00 00 00 00 00 00  ← ID=2, Name="TestProg"
...（其余 126 条全 0）
```
- `83` = 131 = RspGetProgramListInfos 程序列表获取响应
- `80 0A 00 00` = 2688 = bodyLength
- `A0 0A` = 16+2688 = 2704 = packetLength

---

## 九、程序信息获取（FID=132→133）

### 定义

下位机以字节数组 `Global_ProgramInfo[5000]` 承载程序信息，
内部结构对应 Excel "数组定义" 中的 `programInfo[5000]`：

```
[1...20]     程序基本信息
[21...40]    预留
[41...380]   参数窗口
[381...400]  预留
[401...460]  曲线点
[461...469]  预留
[471...5000] 填充
```

### 请求/响应

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 132 | 请求程序信息（body 为空） |
| 设备→PC | 133 | 返回程序信息（body = 5000 字节数组） |

**请求示例**：
```
10 00 00 00 84 00 00 00 01 00 00 00 00 00 00 00
```
- `84` = 132 = ReqGetProgram 程序信息获取请求

**响应示例**：
```
90 13 00 00 85 00 00 00 01 00 00 00 88 13 00 00
[5000 bytes body]
```
- `85` = 133 = RspGetProgram 程序信息获取响应
- `88 13 00 00` = 5000 = bodyLength
- `90 13` = 16+5000 = 5016 = packetLength

---

## 十、IO 状态获取（FID=136→137）

### 定义

```c
typedef struct IoState {
    int32_t  ioInputState;    // IO 输入状态
    int32_t  ioOutputState;   // IO 输出状态
    int32_t  plcInputState;   // PLC 输入状态
    int32_t  plcOutputState;  // PLC 输出状态
    int32_t  stepInputValue;  // 步骤输入值
    int32_t  stepOutputValue; // 步骤输出值
    int32_t  programId;       // 程序 ID
} IoState_T;  // 总大小 = 28 字节
```

### 请求/响应

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 136 | 请求 IO 状态（body 为空） |
| 设备→PC | 137 | 返回 IO 状态（body = IoState_T） |

**请求示例**：
```
10 00 00 00 88 00 00 00 01 00 00 00 00 00 00 00
```
- `88` = 136 = ReqGetIOState

**响应示例**：
```
2C 00 00 00 89 00 00 00 01 00 00 00 1C 00 00 00
[28 bytes IoState_T]
```
- `89` = 137 = RspGetIOState
- `1C` = 28 = bodyLength
- `2C` = 44 = packetLength

---

## 十一、时间同步（FID=138→139）

### 定义

```c
typedef struct Datetime {
    int32_t  timeYear;        // 年
    int32_t  timeMonth;       // 月
    int32_t  timeDay;         // 日
    int32_t  timeHour;        // 时
    int32_t  timeMinute;      // 分
    int32_t  timeSecond;      // 秒
} Datetime_T;  // 总大小 = 24 字节
```

### 请求/响应

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 138 | 同步时间（body = Datetime_T） |
| 设备→PC | 139 | 同步结果（body = result(4B) + Datetime_T(24B) = 28B） |

**请求示例**（设置 2026-07-06 15:30:00）：
```
28 00 00 00 8A 00 00 00 01 00 00 00 18 00 00 00
FA 07 00 00 07 00 00 00 06 00 00 00 0F 00 00 00
1E 00 00 00 00 00 00 00
```
- `8A` = 138 = ReqSyncTime
- `18` = 24 = bodyLength
- `FA 07` = 2026, `07` = 7 月, `06` = 6 日, `0F` = 15 时, `1E` = 30 分, `00` = 0 秒

**响应示例**：
```
2C 00 00 00 8B 00 00 00 01 00 00 00 1C 00 00 00
01 00 00 00 FA 07 00 00 07 00 00 00 06 00 00 00
0F 00 00 00 1E 00 00 00 00 00 00 00
```
- `8B` = 139 = RspSyncTime
- `1C` = 28 = bodyLength（result 4B + Datetime_T 24B）
- `01 00 00 00` = result = 1（同步成功）

---

## 十二、时间获取（FID=140→141）

### 请求/响应

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 140 | 请求当前时间（body 为空） |
| 设备→PC | 141 | 返回当前时间（body = Datetime_T） |

**请求示例**：
```
10 00 00 00 8C 00 00 00 01 00 00 00 00 00 00 00
```
- `8C` = 140 = ReqGetDatetime

**响应示例**：
```
28 00 00 00 8D 00 00 00 01 00 00 00 18 00 00 00
[24 bytes Datetime_T]
```
- `8D` = 141 = RspGetDatetime
- `18` = 24 = bodyLength
- `28` = 40 = packetLength

---

## 十三、曲线数据获取（FID=142→143）

### 定义

```c
typedef struct CurvePointData {
    int32_t  depth;             // 深度
    int32_t  torqueMotor;       // 电机扭矩
    int32_t  torqueSensor;      // 传感器扭矩
    int32_t  voltageSensor;     // 传感器电压
    int32_t  angle;             // 角度
    int32_t  force;             // 压力
    int32_t  rpm;               // 转速
    int32_t  step;              // 步骤
    int32_t  time;              // 时间
    int32_t  count;             // 点数计数
    int32_t  voltageReserved;   // 保留电压
} CurvePointData_T;  // 总大小 = 44 字节（11 × int32_t）
```

### 请求/响应

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 142 | 获取当前曲线点（body = pointIndex 4B） |
| 设备→PC | 143 | 返回曲线点数据（body = CurvePointData_T 44B） |

**请求示例**：
```
14 00 00 00 8E 00 00 00 01 00 00 00 04 00 00 00 00 00 00 00
```
- `8E` = 142 = ReqGetCurvePointDatas
- body = `00 00 00 00` = pointIndex=0

**响应示例**：
```
3C 00 00 00 8F 00 00 00 01 00 00 00 2C 00 00 00
[44 bytes CurvePointData_T]
```
- `8F` = 143 = RspGetCurvePointDatas
- `2C` = 44 = bodyLength
- `3C` = 60 = packetLength

---

## 十四、伺服控制

### 定义

```c
typedef struct ServoTurn {
    int32_t  directionType;   // 1=顺时针, 2=逆时针
    int32_t  turnType;        // 1=速度, 2=角度
    int32_t  rpmSpeed;        // 转速
    int32_t  servoTorque;     // 伺服扭矩
    int32_t  servoAngle;      // 伺服角度
} ServoTurn_T;  // 总大小 = 20 字节（5 × int32_t）
```

### 旋转控制（FID=144→145）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 144 | 控制伺服旋转（body = ServoTurn_T） |
| 设备→PC | 145 | 控制结果（body = result 4B） |

**请求示例**（顺时针/速度/100rpm/扭矩500）：
```
24 00 00 00 90 00 00 00 01 00 00 00 14 00 00 00
01 00 00 00 01 00 00 00 64 00 00 00 F4 01 00 00 00 00 00 00
```
- `90` = 144 = ReqControlServoTurn
- `14` = 20 = bodyLength
- `24` = 36 = packetLength

**响应示例**：
```
14 00 00 00 91 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `91` = 145 = RspControlServoTurn

### 停止控制（FID=146→147）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 146 | 控制伺服停止（body 为空） |
| 设备→PC | 147 | 停止结果（body = result 4B） |

**请求示例**：
```
10 00 00 00 92 00 00 00 01 00 00 00 00 00 00 00
```
- `92` = 146 = ReqControlServoStop

**响应示例**：
```
14 00 00 00 93 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `93` = 147 = RspControlServoStop

---

## 十五、气缸控制

### 定义

```c
typedef struct CylinderCtrl {
    int32_t  directionType;   // 1=向上, 2=向下
    int32_t  counterForce;    // 反作用力（预留，协议仅发 directionType）
} CylinderCtrl_T;  // 总大小 = 8 字节（2 × int32_t）
```

> 协议请求 body 仅发 `directionType`（4 字节）。

### NOK 气缸（FID=148→149）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 148 | 控制 NOK 气缸（body = directionType 4B） |
| 设备→PC | 149 | 控制结果（body = result 4B） |

**请求示例**（directionType=1 向上）：
```
14 00 00 00 94 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `94` = 148 = ReqControlNOKCylinder

**响应示例**：
```
14 00 00 00 95 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `95` = 149 = RspControlNOKCylinder

### 螺丝刀气缸（FID=150→151）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 150 | 控制螺丝刀气缸（body = directionType 4B） |
| 设备→PC | 151 | 控制结果（body = result 4B） |

**请求示例**（directionType=1）：
```
14 00 00 00 96 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `96` = 150 = ReqControlScrewdriverCylinder
- `97` = 151 = RspControlScrewdriverCylinder

### 钳口气缸（FID=152→153）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 152 | 控制钳口气缸（body = directionType 4B） |
| 设备→PC | 153 | 控制结果（body = result 4B） |

**请求示例**（directionType=1）：
```
14 00 00 00 98 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `98` = 152 = ReqControlFeedStrokeCylinder
- `99` = 153 = RspControlFeedStrokeCylinder

---

## 十六、实时数值订阅与推送（FID=154→155）

### 定义

```c
typedef struct CurrentValues {
    int32_t  servoRpmSpeed;    // 电机转速
    int32_t  servoTorque;      // 电机扭矩
    int32_t  servoAngle;       // 电机角度
    int32_t  cylinderPosition; // 气缸位移
} CurrentValues_T;  // 总大小 = 16 字节（4 × int32_t）
```

### 订阅请求（FID=154）

| 方向 | FID | 说明 |
|------|-----|------|
| PC→设备 | 154 | 订阅/取消实时数值（body = subType 4B） |

- subType = 1：订阅（开启周期推送）
- subType = 2：取消订阅（停止推送）

**订阅示例**（subType=1）：
```
14 00 00 00 9A 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```
- `9A` = 154 = ReqSubCurrentValues
- body = `01 00 00 00` = subType=1（订阅）

**取消订阅示例**（subType=0）：
```
14 00 00 00 9A 00 00 00 01 00 00 00 04 00 00 00 00 00 00 00
```
- body = `00 00 00 00` = subType=0（取消订阅）

### 推送响应（FID=155，设备→PC，1s 周期）

订阅成功后，设备每 1 秒主动推送当前实时数值：

```
20 00 00 00 9B 00 00 00 00 00 00 00 10 00 00 00
[16 bytes CurrentValues_T]
```
- `9B` = 155 = RspCurrentValues
- `requestId` = 0（推送消息无请求 ID）
- `10` = 16 = bodyLength
- `20` = 32 = packetLength

**推送示例**（初值：speed=100, torque=200, angle=300, position=400）：
```
20 00 00 00 9B 00 00 00 00 00 00 00 10 00 00 00
64 00 00 00 C8 00 00 00 2C 01 00 00 90 01 00 00
```

---

## 十七、功能码速查表

| 功能 | FID(请求) | FID(响应) | 请求 body | 响应 body |
|------|-----------|-----------|-----------|-----------|
| 心跳 | — | 110 | — | 1×int32 |
| 设置后门参数 | 112 | 113 | BackdoorParam_T(208B) | result(4B) |
| 获取后门参数 | 114 | 115 | — | BackdoorParam_T(208B) |
| 设置系统参数 | 116 | 117 | SystemParam_T(32B) | result(4B) |
| 获取系统参数 | 118 | 119 | — | SystemParam_T(32B) |
| 获取运维信息 | 120 | 121 | — | OpmaintInfo_T(24B) |
| 清除运维计数 | 122 | 123 | OpmaintSet_T(4B) | result(4B) |
| 系统状态推送 | — | 125(推) | — | SysStatePush_T(16B) |
| 报警推送 | — | 127(推) | — | SysError_T(8B) |
| 步骤结果推送 | — | 135(推) | — | ProgramStepResultInfos_T[128](3584B) |
| 保存程序 | 128 | 129 | CurrentProgramInfo_T | result(4B) |
| 获取程序列表 | 130 | 131 | — | ProgramListInfo_T[128](2688B) |
| 获取程序信息 | 132 | 133 | — | uint8_t[5000] |
| IO 状态获取 | 136 | 137 | — | IoState_T(28B) |
| 同步时间 | 138 | 139 | Datetime_T(24B) | result(4B) + Datetime_T(24B) |
| 获取时间 | 140 | 141 | — | Datetime_T(24B) |
| 曲线数据获取 | 142 | 143 | pointIndex(4B) | CurvePointData_T(44B) |
| 伺服旋转 | 144 | 145 | ServoTurn_T(20B) | result(4B) |
| 伺服停止 | 146 | 147 | — | result(4B) |
| NOK 气缸 | 148 | 149 | directionType(4B) | result(4B) |
| 螺丝刀气缸 | 150 | 151 | directionType(4B) | result(4B) |
| 钳口气缸 | 152 | 153 | directionType(4B) | result(4B) |
| 实时数值订阅 | 154 | 155(推) | subType(4B) | CurrentValues_T(16B) |

---

## 十八、常用请求 HEX 速查

以下 requestId 均为 1，使用时替换为实际值：

### 获取类（PC 发送）

```
后门参数获取:  10 00 00 00 72 00 00 00 01 00 00 00 00 00 00 00
系统参数获取:  10 00 00 00 76 00 00 00 01 00 00 00 00 00 00 00
运维信息获取:  10 00 00 00 78 00 00 00 01 00 00 00 00 00 00 00
程序列表获取:  10 00 00 00 82 00 00 00 01 00 00 00 00 00 00 00
程序信息获取:  10 00 00 00 84 00 00 00 01 00 00 00 00 00 00 00
IO 状态获取:   10 00 00 00 88 00 00 00 01 00 00 00 00 00 00 00
时间获取:      10 00 00 00 8C 00 00 00 01 00 00 00 00 00 00 00
伺服停止:      10 00 00 00 92 00 00 00 01 00 00 00 00 00 00 00
```

### 设置类（PC 发送，body 替换为实际值）

```
后门参数设置:  E0 00 00 00 70 00 00 00 01 00 00 00 D0 00 00 00 [208bytes body]
系统参数设置:  30 00 00 00 74 00 00 00 01 00 00 00 20 00 00 00 [32bytes body]
运维清零(同步带): 14 00 00 00 7A 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
运维清零(润滑油): 14 00 00 00 7A 00 00 00 01 00 00 00 04 00 00 00 02 00 00 00
程序保存:     14 00 00 00 80 00 00 00 01 00 00 00 04 00 00 00 [4bytes body]
时间同步:     28 00 00 00 8A 00 00 00 01 00 00 00 18 00 00 00 [24bytes Datetime_T]
曲线数据获取: 14 00 00 00 8E 00 00 00 01 00 00 00 04 00 00 00 [4bytes pointIndex]
伺服旋转:     24 00 00 00 90 00 00 00 01 00 00 00 14 00 00 00 [20bytes ServoTurn_T]
NOK气缸(向上):14 00 00 00 94 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
NOK气缸(向下):14 00 00 00 94 00 00 00 01 00 00 00 04 00 00 00 02 00 00 00
螺丝刀气缸(1): 14 00 00 00 96 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
钳口气缸(1):  14 00 00 00 98 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
订阅实时数值: 14 00 00 00 9A 00 00 00 01 00 00 00 04 00 00 00 01 00 00 00
```

---

*更多 FID 定义见 `fds_protocol.h`，数据结构体定义见 `fds_data.h`。*
