/**
 * @file    AnybusNP40Slave.h
 * @brief   Anybus NP40 模块驱动 (Profinet / EtherCAT / EtherNet/IP)
 *
 * 通信方式：UART (Modbus-RTU 协议)
 *
 * 用法：
 *   main 初始化阶段调用  NP40_Init();
 *   10ms 定时器中调用   NP40_Task();     // 初始化 + 状态机 + PDO 数据交换
 */

#ifndef ANYBUS_NP40_SLAVE_H
#define ANYBUS_NP40_SLAVE_H

#include "main.h"
#include <stdint.h>


/* ============================================================================
 * NP40_Debug — 调试打印宏
 * ==========================================================================*/
#ifndef NP40_DEBUG_ENABLE
#define NP40_DEBUG_ENABLE  1
#endif

#if NP40_DEBUG_ENABLE
    #define NP40_Debug(...)   bsp_usart_printf(&s_usart1, __VA_ARGS__)
#else
    #define NP40_Debug(...)   ((void)0)
#endif


/* ============================================================================
 * 常量定义
 * ============================================================================*/

#define NP40_RX_BUF_SIZE      256
#define NP40_TX_BUF_SIZE      256

/* NP40 复位引脚 (PE4) */
#define NP40_RST_PORT         GPIOE
#define NP40_RST_PIN          GPIO_PIN_4

/* NP40 寄存器地址 */
#define NP40_REG_DEV_TYPE     0x5003      /* 设备类型 */
#define NP40_REG_PROTOCOL     0x5004      /* 协议类型 */
#define NP40_REG_ADDR_5101    0x5101      /* 地址配置1 */
#define NP40_REG_ADDR_5102    0x5102      /* 地址配置2 */
#define NP40_REG_ADDR_5103    0x5103      /* 地址配置3 */
#define NP40_REG_PDO_DATA     0x0FFD      /* PDO 数据区起始地址 */

/* 协议类型返回值 */
#define NP40_PROTO_PROFINET   0x89
#define NP40_PROTO_ETHERIP    0x9B
#define NP40_PROTO_ETHERCAT   0x87
#define NP40_PROTO_MODBUSTCP  0x93

/* PDO 尺寸 */
#define NP40_PDO_INPUT_SIZE   128         /* 我们发给 NP40 的字节数 (64 regs) */
#define NP40_PDO_OUTPUT_SIZE  128         /* NP40 返回给我们的字节数 (64 regs) */


/* ============================================================================
 * 类型定义
 * ============================================================================*/

/* NP40 状态 */
enum Np40State {
    NP40_STATE_INIT       = 0,    /* 初始化中 */
    NP40_STATE_IDLE       = 1,    /* 空闲 */
    NP40_STATE_RUNNING    = 2,    /* 运行中 */
    NP40_STATE_ERROR      = 0xFF
};

/* 读命令阶段 */
enum Np40ReadType {
    NP40_READ_DEV_TYPE    = 0,    /* 读 0x5003 */
    NP40_READ_PROTOCOL    = 1,    /* 读 0x5004 */
    NP40_READ_ADDR_5101   = 2,    /* 写 0x5101 */
    NP40_READ_ADDR_5102   = 3,    /* 写 0x5102 */
    NP40_READ_ADDR_5103   = 4,    /* 写 0x5103 */
    NP40_READ_PDO         = 5     /* 读写 PDO 数据 */
};


/* ============================================================================
 * 结构体定义
 * ============================================================================*/

/* PDO 数据子结构体 */
struct NP40_Pdo {
    uint8_t  beatState;
    uint8_t  beatCnt;
    uint16_t tickCnt;
    uint8_t  cntByte0;
    uint8_t  txBuf[256];
    uint8_t  output[NP40_PDO_OUTPUT_SIZE];
    uint8_t  plcConnected;
    uint16_t plcHbCnt;
};

/* NP40 模块状态结构体*/
struct NP40 {
    /* 运行状态 */
    uint8_t  state;
    uint8_t  readType;
    uint8_t  protocol;
    uint8_t  present;

    /* 缓冲区 */
    uint8_t  rxBuf[NP40_RX_BUF_SIZE];
    uint16_t rxLen;
    uint8_t  txBuf[NP40_TX_BUF_SIZE];

    /* 初始化 */
    uint8_t  procPending;
    uint16_t initTimeout;

    /* 命令重试 */
    uint16_t retry;

    /* 探活重试 */
    uint8_t  probeRetryCnt;
    uint16_t probeRetryTmo;

    /* 非阻塞复位 */
    uint8_t  resetActive;
    uint8_t  resetPhase;
    uint32_t resetTick;

    /* 配置地址 */
    uint16_t addr5101;
    uint16_t addr5102;
    uint16_t addr5103;

    /* PDO 数据 */
    struct NP40_Pdo pdo;
};

/* 全局实例声明 */
extern struct NP40 NP40;


/* ============================================================================
 * NP40 初始化与周期处理
 * ============================================================================*/

/* 初始化 NP40 模块 (GPIO + USART + 非阻塞复位 + 探活) */
void NP40_Init(void);

/* NP40 主状态机 (初始化 + PDO 交换) — 10ms 周期调用 */
void NP40_Task(void);

/* NP40 硬件复位 (非阻塞，状态机在 NP40_Task 中执行) */
void NP40_Reset(void);

/* CRC16-Modbus 校验 */
uint16_t NP40_CRC16(const uint8_t *buf, uint8_t len);

/* 发送读寄存器命令 */
void NP40_SendReadCmd(uint16_t regAddr);

/* 发送写寄存器命令 */
void NP40_SendWriteCmd(uint16_t regAddr, uint16_t data);

/* 发送 PDO 数据 */
void NP40_SendPDO(const uint8_t *data, uint16_t datalen);


/* ============================================================================
 * 状态查询
 * ============================================================================*/

uint8_t NP40_GetState(void);
uint8_t NP40_GetProtocol(void);
uint8_t NP40_GetPdoByte0(void);
uint8_t NP40_GetPdoByte1(void);
uint8_t NP40_GetOutputByte0(void);
uint8_t NP40_GetPlcConnected(void);
uint16_t NP40_GetPlcHbCnt(void);

#endif /* ANYBUS_NP40_SLAVE_H */
