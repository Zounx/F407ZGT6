/**
 * @file    fds_handler.c
 * @brief   FDS 业务处理层——指令分发与业务逻辑
 *
 * 通过 WIZnet TCP Server（socket 0）接收上位机指令并处理。
 */
#include "fds_handler.h"
#include "fds_param.h"
#include "fds_protocol.h"
#include "ETH_WIZdemo.h"
#include "wizchip_conf.h"
#include "wiz_socket.h"
#include <string.h>
#include "stm32f4xx_hal.h"

/* ============================================================================
 * 内部常量
 * ============================================================================ */

/** FDS 使用 TCP Server socket 0 */
#define HANDLER_SOCKET      0

/** FDS TCP Server 端口 */
#define HANDLER_PORT        5002

/** 接收缓冲区大小 */
#define RX_BUF_SIZE     PACKET_MAX_SIZE

/** 发送缓冲区大小 */
#define TX_BUF_SIZE     PACKET_MAX_SIZE

/** 心跳发送间隔（ms） */
#define HEARTBEAT_INTERVAL      1000

/* ============================================================================
 * 内部变量
 * ============================================================================ */

/** 接收数据缓冲区 */
static uint8_t s_rx_buf[RX_BUF_SIZE];

/** 已接收但尚未处理的数据长度 */
static int32_t s_rx_len;

/** 发送缓冲区 */
static uint8_t s_tx_buf[TX_BUF_SIZE];

/** requestId 递增计数器 */
static int32_t s_request_id;

/** 上次心跳发送时间戳 */
static uint32_t s_last_heartbeat_tick;

/* ============================================================================
 * 内部函数前置声明
 * ============================================================================ */

static void SendResponse(int32_t function_id, int32_t request_id,
                          const void *body, int32_t body_len);

/* ============================================================================
 * Handler 函数实现
 * ============================================================================ */

/** 后门参数设置 */
static void handle_set_backdoor_param(const struct PacketHeader *hdr,
                                       const uint8_t *body)
{
    ETH_DEBUG("[FDS] SetBackDoorParam (fid=%d)\r\n", hdr->function_id);
    /* TODO: 解析 body 中的系列型号/序列号/固件版本等 */
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(BackdoorParam_T)) {
        const BackdoorParam_T *p = (const void *)body;
        memcpy(&Global_BackdoorParam, p, sizeof(BackdoorParam_T));
    }
    {
        int32_t result = 1; /* 先返回成功 */
        Result_T rsp = {result};
        SendResponse(FID_RSP_SET_BACKDOOR_PARAM, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 后门参数获取 */
static void handle_get_backdoor_param(const struct PacketHeader *hdr,
                                       const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetBackDoorParam (fid=%d)\r\n", hdr->function_id);
    {
        SendResponse(FID_RSP_GET_BACKDOOR_PARAM, hdr->request_id,
                     &Global_BackdoorParam, sizeof(Global_BackdoorParam));
    }
}

/** 系统参数设置 */
static void handle_set_system_param(const struct PacketHeader *hdr,
                                     const uint8_t *body)
{
    ETH_DEBUG("[FDS] SetSystemParam (fid=%d)\r\n", hdr->function_id);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(SystemParam_T)) {
        const SystemParam_T *p = (const void *)body;
        /* TODO: 更新系统参数到数据结构 */
        memcpy(&Global_SystemParam, p, sizeof(SystemParam_T));
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SET_SYSTEM_PARAM, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 系统参数获取 */
static void handle_get_system_param(const struct PacketHeader *hdr,
                                     const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetSystemParam (fid=%d)\r\n", hdr->function_id);
    {
        SendResponse(FID_RSP_GET_SYSTEM_PARAM, hdr->request_id,
                     &Global_SystemParam, sizeof(Global_SystemParam));
    }
}

/** 运维信息设置 */
static void handle_set_opmaint(const struct PacketHeader *hdr,
                                const uint8_t *body)
{
    ETH_DEBUG("[FDS] SetOpmaintInfo (fid=%d)\r\n", hdr->function_id);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(OpmaintSet_T)) {
        const OpmaintSet_T *p = (const void *)body;
        if (p->dataType == 1) {
            /* 清除同步带计数 */
            Global_OpmaintInfo.suggestedReplaceBelt     = 0;
            Global_OpmaintInfo.accumulatedRunningTimeBelt = 0;
            ETH_DEBUG("[FDS] Clear belt counters\r\n");
        } else if (p->dataType == 2) {
            /* 清除润滑油计数 */
            Global_OpmaintInfo.suggestedReplaceLube     = 0;
            Global_OpmaintInfo.accumulatedRunningTimeLube = 0;
            ETH_DEBUG("[FDS] Clear lube counters\r\n");
        }
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SET_OPMAINT_INFO, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 运维信息获取 */
static void handle_get_opmaint(const struct PacketHeader *hdr,
                                const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetOpmaintInfo (fid=%d)\r\n", hdr->function_id);
    (void)body;
    {
        SendResponse(FID_RSP_GET_OPMAINT_INFO, hdr->request_id,
                     &Global_OpmaintInfo, sizeof(Global_OpmaintInfo));
    }
}

/** 时间同步 */
static void handle_sync_time(const struct PacketHeader *hdr,
                              const uint8_t *body)
{
    ETH_DEBUG("[FDS] SyncTime (fid=%d)\r\n", hdr->function_id);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(Datetime_T)) {
        const Datetime_T *dt = (const void *)body;
        /* TODO: 设置 RTC 时间 */
        /* TODO: 回传同步后的时间 */
        (void)dt;
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SYNC_TIME, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** IO 状态获取 */
static void handle_get_io_state(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetIOState (fid=%d)\r\n", hdr->function_id);
    /* TODO: 采集当前 IO 状态并回传 */
    {
        IoState_T rsp;
        memset(&rsp, 0, sizeof(rsp));
        rsp.ioInputState  = 0;
        rsp.ioOutputState = 0;
        SendResponse(FID_RSP_GET_IO_STATE, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 程序保存 */
static void handle_save_program(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{
    ETH_DEBUG("[FDS] SaveProgram (len=%d)\r\n", hdr->body_length);
    /* TODO: 解析程序数据并保存到 Flash/SD */
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SAVE_PROGRAM, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 程序列表获取 */
static void handle_get_program_list(const struct PacketHeader *hdr,
                                     const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetProgramList\r\n");
    /* TODO: 回传程序列表 */
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_GET_PROGRAM_LIST, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 程序获取 */
static void handle_get_program(const struct PacketHeader *hdr,
                                const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetProgram\r\n");
    /* TODO: 回传指定程序数据 */
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_GET_PROGRAM, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 伺服旋转 */
static void handle_servo_turn(const struct PacketHeader *hdr,
                               const uint8_t *body)
{
    ETH_DEBUG("[FDS] ServoTurn\r\n");
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(ServoTurn_T)) {
        const ServoTurn_T *s = (const void *)body;
        /* TODO: 控制伺服电机 */
        (void)s;
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SERVO_TURN, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 伺服停止 */
static void handle_servo_stop(const struct PacketHeader *hdr,
                               const uint8_t *body)
{
    ETH_DEBUG("[FDS] ServoStop\r\n");
    /* TODO: 停止伺服 */
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SERVO_STOP, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 气缸控制通用处理 */
static void handle_cylinder_ctrl(const struct PacketHeader *hdr,
                                  const uint8_t *body,
                                  int32_t rsp_fid,
                                  const char *name)
{
    ETH_DEBUG("[FDS] %s\r\n", name);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(CylinderCtrl_T)) {
        const CylinderCtrl_T *c = (const void *)body;
        /* TODO: 控制气缸 */
        (void)c;
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(rsp_fid, hdr->request_id, &rsp, sizeof(rsp));
    }
}
static void handle_nok_cylinder(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{ handle_cylinder_ctrl(hdr, body, FID_RSP_NOK_CYLINDER, "NOKCylinder"); }
static void handle_screwdriver_cylinder(const struct PacketHeader *hdr,
                                         const uint8_t *body)
{ handle_cylinder_ctrl(hdr, body, FID_RSP_SCREWDRIVER_CYLINDER, "ScrewdriverCylinder"); }
static void handle_feedstroke_cylinder(const struct PacketHeader *hdr,
                                        const uint8_t *body)
{ handle_cylinder_ctrl(hdr, body, FID_RSP_FEEDSTROKE_CYLINDER, "FeedStrokeCylinder"); }

/** 订阅实时数值 */
static void handle_sub_current_values(const struct PacketHeader *hdr,
                                       const uint8_t *body)
{
    ETH_DEBUG("[FDS] SubCurrentValues\r\n");
    /* TODO: 处理订阅/取消订阅 */
}

/** 曲线获取 */
static void handle_get_curve(const struct PacketHeader *hdr,
                              const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetCurvePoints\r\n");
    /* TODO: 回传曲线数据 */
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_GET_CURVE_POINTS, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 时间获取 */
static void handle_get_datetime(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{
    ETH_DEBUG("[FDS] GetDatetime\r\n");
    {
        Datetime_T rsp;
        memset(&rsp, 0, sizeof(rsp));
        /* TODO: 读取 RTC */
        SendResponse(FID_RSP_GET_DATETIME, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/* ============================================================================
 * 分发表
 * ============================================================================ */

static const struct PushEntry s_dispatch_table[] = {
    /* 后门参数 */
    {FID_REQ_SET_BACKDOOR_PARAM,     handle_set_backdoor_param},
    {FID_REQ_GET_BACKDOOR_PARAM,     handle_get_backdoor_param},

    /* 系统参数 */
    {FID_REQ_SET_SYSTEM_PARAM,       handle_set_system_param},
    {FID_REQ_GET_SYSTEM_PARAM,       handle_get_system_param},

    /* 运维信息 */
    {FID_REQ_GET_OPMAINT_INFO,       handle_get_opmaint},
    {FID_REQ_SET_OPMAINT_INFO,       handle_set_opmaint},

    /* 时间同步 */
    {FID_REQ_SYNC_TIME,              handle_sync_time},
    {FID_REQ_GET_DATETIME,           handle_get_datetime},

    /* IO 状态 */
    {FID_REQ_GET_IO_STATE,           handle_get_io_state},

    /* 程序管理 */
    {FID_REQ_SAVE_PROGRAM,           handle_save_program},
    {FID_REQ_GET_PROGRAM_LIST,       handle_get_program_list},
    {FID_REQ_GET_PROGRAM,            handle_get_program},

    /* 曲线 */
    {FID_REQ_GET_CURVE_POINTS,       handle_get_curve},

    /* 伺服控制 */
    {FID_REQ_SERVO_TURN,             handle_servo_turn},
    {FID_REQ_SERVO_STOP,             handle_servo_stop},

    /* 气缸控制 */
    {FID_REQ_NOK_CYLINDER,           handle_nok_cylinder},
    {FID_REQ_SCREWDRIVER_CYLINDER,   handle_screwdriver_cylinder},
    {FID_REQ_FEEDSTROKE_CYLINDER,    handle_feedstroke_cylinder},

    /* 订阅 */
    {FID_REQ_SUB_CURRENT_VALUES,     handle_sub_current_values},
};

static const int s_dispatch_table_size =
    sizeof(s_dispatch_table) / sizeof(s_dispatch_table[0]);

/* ============================================================================
 * 内部函数实现
 * ============================================================================ */

/**
 * @brief  通过 TCP Server socket 发送响应
 */
static void SendResponse(int32_t function_id, int32_t request_id,
                          const void *body, int32_t body_len)
{
    int total_len;

    total_len = ProtocolEncode(s_tx_buf, function_id, request_id,
                               body, body_len);
    if (total_len > 0) {
        uint8_t status = getSn_SR(HANDLER_SOCKET);
        if (status == SOCK_ESTABLISHED) {
            /* 临时切非阻塞 */
            uint8_t nb = SOCK_IO_NONBLOCK;
            wiz_ctlsocket(HANDLER_SOCKET, CS_SET_IOMODE, &nb);
            int32_t ret = wiz_send(HANDLER_SOCKET, s_tx_buf, (uint16_t)total_len);
            ETH_DEBUG("[FDS] wiz_send=%d (fid=%d, len=%d)\r\n",
                      ret, function_id, total_len);
        } else {
            ETH_DEBUG("[FDS] skip send, status=0x%02x\r\n", status);
        }
    } else {
        ETH_DEBUG("[FDS] ProtocolEncode FAILED (fid=%d)\r\n", function_id);
    }
}

/**
 * @brief  尝试从 TCP Server socket 接收数据并分发
 */
static void RecvAndDispatch(void)
{
    int32_t recv_len;
    int32_t total_len;
    struct PacketHeader hdr;

    if (s_rx_len >= RX_BUF_SIZE) {
        /* 缓冲区满，丢弃全部数据防止死锁 */
        ETH_DEBUG("[FDS] RX buf overflow, flushing\r\n");
        s_rx_len = 0;
    }

    /* 尝试接收新数据 */
    recv_len = wiz_recv(HANDLER_SOCKET, s_rx_buf + s_rx_len,
                        RX_BUF_SIZE - s_rx_len);
    if (recv_len <= 0) return;

    s_rx_len += recv_len;

    /* 循环解析缓冲区中的所有完整报文 */
    while (s_rx_len >= PACKET_HEADER_SIZE) {
        /* 尝试解析头部 */
        if (ProtocolDecodeHeader(s_rx_buf, s_rx_len, &hdr) != 0) {
            /* 数据不足以构成完整报文，等待更多数据 */
            if (s_rx_len >= PACKET_HEADER_SIZE) {
                /* 数据错位，逐字节丢弃直到对齐 */
                s_rx_len -= 1;
                memmove(s_rx_buf, s_rx_buf + 1, s_rx_len);
                continue;
            }
            break;
        }

        total_len = hdr.packet_length;

        if (s_rx_len < total_len) {
            /* 还没有收完整个报文，等待剩余数据 */
            break;
        }

        ETH_DEBUG("[FDS] Dispatch FID=%ld (len=%ld)\r\n",
                  hdr.function_id, total_len);

        /* 分发完整报文 */
        ProtocolDispatch(s_rx_buf, total_len,
                         s_dispatch_table, s_dispatch_table_size);

        /* 移除已处理的报文 */
        s_rx_len -= total_len;
        if (s_rx_len > 0) {
            memmove(s_rx_buf, s_rx_buf + total_len, s_rx_len);
        }
    }
}

/* ============================================================================
 * 外部接口实现
 * ============================================================================ */

void HandlerInit(void)
{
    /* 初始化数据层 */
    DataInit();

    /* 清空接收缓冲区 */
    s_rx_len = 0;
    s_request_id = 0;
    (void)s_request_id;     /* 预留：下位机主动推送时使用 */
    s_last_heartbeat_tick = 0;

    /* 打开 TCP Server socket 0 */
    wiz_socket(HANDLER_SOCKET, Sn_MR_TCP, HANDLER_PORT, SF_IO_NONBLOCK);
    wiz_listen(HANDLER_SOCKET);

    ETH_DEBUG("[FDS] Handler initialized, TCP Server listening on port %d\r\n",
              HANDLER_PORT);
}

void HandlerTask(void)
{
    uint8_t status;

    /* 检查 TCP Server socket 状态 */
    status = getSn_SR(HANDLER_SOCKET);

    if (status == SOCK_ESTABLISHED) {
        /* 有客户端连接，接收并处理数据 */
        RecvAndDispatch();

        /* 周期心跳：设备主动发送保活信号 */
        if (HAL_GetTick() - s_last_heartbeat_tick >= HEARTBEAT_INTERVAL) {
            s_last_heartbeat_tick = HAL_GetTick();
            Heartbeat_T body = {(int32_t)HAL_GetTick()};
            SendResponse(FID_HEARTBEAT, 0, &body, sizeof(body));
        }
    } else if (status == SOCK_CLOSE_WAIT) {
        /* 客户端断开，重新监听 */
        wiz_close(HANDLER_SOCKET);
        wiz_socket(HANDLER_SOCKET, Sn_MR_TCP, HANDLER_PORT, SF_IO_NONBLOCK);
        wiz_listen(HANDLER_SOCKET);
        s_rx_len = 0;
        ETH_DEBUG("[FDS] Client disconnected, re-listening\r\n");
    } else if (status == SOCK_CLOSED) {
        /* 首次启动或 socket 异常关闭，重新打开 */
        wiz_socket(HANDLER_SOCKET, Sn_MR_TCP, HANDLER_PORT, SF_IO_NONBLOCK);
        wiz_listen(HANDLER_SOCKET);
        s_rx_len = 0;
        ETH_DEBUG("[FDS] Socket reopened, listening\r\n");
    }
    /* 其他状态（LISTEN/INIT 等）不处理 */
}
