/**
 * @file    Profinet.h
 * @brief   PROFINET — Anybus NP40 模块驱动 (移植版)
 *
 * 通信方式: UART (Modbus-RTU 协议)
 * NP40 激活流程:
 *   1. 硬件复位 (PE4 拉低 500ms)
 *   2. 发送读命令 0x5004 → 返回协议类型 (0x89=PN)
 *   3. 发送写命令 0x5101/0x5102/0x5103 配置地址
 *   4. 循环读写 PDO 数据 (0x0FFD)
 *
 * 适配 F407ZGT6 工程: 使用 bsp_usart 驱动 (USART2, PA2/PA3, DMA)
 */

#ifndef PROFINET_H
#define PROFINET_H

#include "main.h"
#include <string.h>
#include <stdint.h>

/* 帧最大长度 */
#define PN_RX_BUF_SIZE      256
#define PN_TX_BUF_SIZE      256

/* NP40 复位引脚 (PE4) */
#define NP40_RST_PORT       GPIOE
#define NP40_RST_PIN        GPIO_PIN_4

/* NP40 寄存器地址 */
#define PN_REG_DEV_TYPE     0x5003      /* 设备类型 */
#define PN_REG_PROTOCOL     0x5004      /* 协议类型 */
#define PN_REG_ADDR_5101    0x5101      /* 地址配置1 */
#define PN_REG_ADDR_5102    0x5102      /* 地址配置2 */
#define PN_REG_ADDR_5103    0x5103      /* 地址配置3 */
#define PN_REG_PDO_DATA     0x0FFD      /* PDO 数据区起始地址 */

/* 协议类型返回值 */
#define PN_PROTO_PROFINET   0x89
#define PN_PROTO_ETHERIP    0x9B
#define PN_PROTO_ETHERCAT   0x87
#define PN_PROTO_MODBUSTCP  0x93

/* PN 状态 */
typedef enum {
    PN_STATE_INIT       = 0,    /* 初始化中 */
    PN_STATE_IDLE       = 1,    /* 空闲 */
    PN_STATE_RUNNING    = 2,    /* 运行中 */
    PN_STATE_ERROR      = 0xFF
} PN_State;

/* 读命令返回类型 */
typedef enum {
    PN_READ_DEV_TYPE    = 0,    /* 读 0x5003 */
    PN_READ_PROTOCOL    = 1,    /* 读 0x5004 */
    PN_READ_ADDR_5101   = 2,    /* 读/写 0x5101 */
    PN_READ_ADDR_5102   = 3,    /* 读/写 0x5102 */
    PN_READ_ADDR_5103   = 4,    /* 读/写 0x5103 */
    PN_READ_PDO         = 5     /* 读写 PDO 数据 */
} PN_ReadType;

/******************************************************************************
 * 函数声明
 *****************************************************************************/

/* 初始化 PN 模块 (GPIO + 复位 + 启动通信) */
void PN_Init(void);

/* PN 周期处理 (在协调器或主循环中调用) */
void PN_Task(void);

/* 发送读寄存器命令 */
void PN_SendReadCmd(uint16_t reg_addr);

/* 发送写寄存器命令 (16位数据) */
void PN_SendWriteCmd(uint16_t reg_addr, uint16_t data);

/* NP40 硬件复位 */
void PN_Reset(void);

/* CRC16-Modbus 校验 */
uint16_t PN_CRC16(uint8_t *buf, uint8_t len);

/* 发送 PDO 数据 */
void PN_SendPDO(uint8_t *data, uint16_t datalen);

/* 获取当前 PN 状态 */
PN_State PN_GetState(void);

/* 获取协议类型 */
uint8_t PN_GetProtocol(void);

/* 获取 PDO Byte0 (每 1s +1 计数器) */
uint8_t PN_GetPdoByte0(void);

/* 获取 PDO Byte1 (200ms 翻转 0/1 交替) */
uint8_t PN_GetPdoByte1(void);

/* 获取 OutputPDO byte0 (NPNP 指令: 0=PNP, 1=NPN) */
uint8_t PN_GetOutputByte0(void);

/* 获取 PLC 连接状态 (0=未连接, 1=已连接) */
uint8_t PN_GetPlcConnected(void);

/* 获取 PLC 心跳计数 */
uint16_t PN_GetPlcHbCnt(void);

#endif /* PROFINET_H */
