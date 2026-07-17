/**
 * @file    fds_protocol.h
 * @brief   FDS 协议编解码层——报文打包/解析/分发
 *
 * == 职责边界 ==
 * - 所有数据类型的 struct 定义在 fds_data.h（参数表）
 * - 协议层只关心"怎么编解码"，不定义"数据长什么样"
 * - body 结构体在 fds_data.h 中定义，协议层直接用
 *
 * 报文格式：[packetLength:4][functionId:4][requestId:4][bodyLength:4][body...]
 * 前 16 字节为固定头部，body 为变长数据（类型见 fds_data.h）。
 */
#ifndef FDS_PROTOCOL_H
#define FDS_PROTOCOL_H

#include <stdint.h>
#include "fds_data.h"

/* ============================================================================
 * 功能码定义
 * ============================================================================ */

enum FunctionId {
    /* 心跳 */
    FID_HEARTBEAT                   = 110,

    /* 后门参数 */
    FID_REQ_SET_BACKDOOR_PARAM      = 112,
    FID_RSP_SET_BACKDOOR_PARAM      = 113,
    FID_REQ_GET_BACKDOOR_PARAM      = 114,
    FID_RSP_GET_BACKDOOR_PARAM      = 115,

    /* 系统参数 */
    FID_REQ_SET_SYSTEM_PARAM        = 116,
    FID_RSP_SET_SYSTEM_PARAM        = 117,
    FID_REQ_GET_SYSTEM_PARAM        = 118,
    FID_RSP_GET_SYSTEM_PARAM        = 119,

    /* 运维信息 */
    FID_REQ_GET_OPMAINT_INFO        = 120,
    FID_RSP_GET_OPMAINT_INFO        = 121,
    FID_REQ_SET_OPMAINT_INFO        = 122,
    FID_RSP_SET_OPMAINT_INFO        = 123,

    /* 系统状态/报警推送 */
    FID_SEND_SYSTEM_STATE           = 125,
    FID_SEND_SYSTEM_ERROR           = 127,

    /* 程序管理 */
    FID_REQ_SAVE_PROGRAM            = 128,
    FID_RSP_SAVE_PROGRAM            = 129,
    FID_REQ_GET_PROGRAM_LIST        = 130,
    FID_RSP_GET_PROGRAM_LIST        = 131,
    FID_REQ_GET_PROGRAM             = 132,
    FID_RSP_GET_PROGRAM             = 133,

    /* 程序执行结果推送 */
    FID_SEND_STEP_RESULT            = 135,

    /* IO 状态 */
    FID_REQ_GET_IO_STATE            = 136,
    FID_RSP_GET_IO_STATE            = 137,

    /* 时间同步 */
    FID_REQ_SYNC_TIME               = 138,
    FID_RSP_SYNC_TIME               = 139,
    FID_REQ_GET_DATETIME            = 140,
    FID_RSP_GET_DATETIME            = 141,

    /* 曲线数据 */
    FID_REQ_GET_CURVE_POINTS        = 142,
    FID_RSP_GET_CURVE_POINTS        = 143,

    /* 伺服控制 */
    FID_REQ_SERVO_TURN              = 144,
    FID_RSP_SERVO_TURN              = 145,
    FID_REQ_SERVO_STOP              = 146,
    FID_RSP_SERVO_STOP              = 147,

    /* 气缸控制 */
    FID_REQ_NOK_CYLINDER            = 148,
    FID_RSP_NOK_CYLINDER            = 149,
    FID_REQ_SCREWDRIVER_CYLINDER    = 150,
    FID_RSP_SCREWDRIVER_CYLINDER    = 151,
    FID_REQ_FEEDSTROKE_CYLINDER     = 152,
    FID_RSP_FEEDSTROKE_CYLINDER     = 153,

    /* 实时数值订阅 */
    FID_REQ_SUB_CURRENT_VALUES      = 154,
    FID_RSP_CURRENT_VALUES          = 155,
};

/* ============================================================================
 * 通用报文头部
 * ============================================================================ */

#define PACKET_HEADER_SIZE  16  /* 4 个 int32_t */

/**
 * @brief  报文头部结构体
 * @note   网络字节序：小端（与 STM32 一致）
 */
struct PacketHeader {
    int32_t  packet_length;       /**< 包总长度（含头部） */
    int32_t  function_id;         /**< 功能码 */
    int32_t  request_id;          /**< 请求 ID（上位机生成，下位机原样返回） */
    int32_t  body_length;         /**< 数据体长度 */
};

/** 协议最大包长 */
#define PACKET_MAX_SIZE     8*1024        // 最大包长度（程序列表 5640 字节）

/* ============================================================================
 * 编解码接口
 * ============================================================================ */

/**
 * @brief  打包请求/响应报文
 * @param  buf         输出缓冲区（>= PACKET_MAX_SIZE）
 * @param  function_id 功能码
 * @param  request_id  请求 ID
 * @param  body        数据体指针（可为 NULL）
 * @param  body_len    数据体长度（字节）
 * @return 包总长度（字节），<= 0 表示失败
 */
int ProtocolEncode(uint8_t *buf, int32_t function_id,
                   int32_t request_id,
                   const void *body, int32_t body_len);

/**
 * @brief  解析报文头
 * @param  buf     接收缓冲区
 * @param  buf_len 缓冲区长度
 * @param  hdr     [out] 解析出的头部
 * @return 0=成功, -1=数据不足, -2=格式错误
 */
int ProtocolDecodeHeader(const uint8_t *buf, int32_t buf_len,
                         struct PacketHeader *hdr);

/* ============================================================================
 * 消息分发
 * ============================================================================ */

/** 消息处理函数类型 */
typedef void (*HandlerFn)(const struct PacketHeader *hdr,
                           const uint8_t *body);

/** 分发表条目 */
struct PushEntry {
    int32_t      function_id;
    HandlerFn    handler;
};

/**
 * @brief  根据 function_id 分发已收到的完整报文
 * @param  buf        含完整头部的报文缓冲区
 * @param  len        报文总长度
 * @param  table      分发表
 * @param  table_size 表项数量
 * @return 0=已分发, 1=无对应处理器（忽略）
 */
int ProtocolDispatch(const uint8_t *buf, int32_t len,
                     const struct PushEntry *table,
                     int table_size);

#endif /* FDS_PROTOCOL_H */
