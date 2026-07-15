/**
 * @file    fds_handler.c
 * @brief   FDS 业务处理层——指令分发与业务逻辑
 *
 * 通过 WIZnet TCP Server（socket 0）接收上位机指令并处理。
 */
#include "fds_handler.h"
#include "fds_param.h"
#include "fds_data.h"
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

/* ============================================================================
 * 内部变量
 * ============================================================================ */

/** 接收数据缓冲区 */
static uint8_t s_rx_buf[RX_BUF_SIZE];

/** 接收缓冲区读指针 */
static int32_t s_rx_rd;

/** 接收缓冲区写指针 */
static int32_t s_rx_wr;

/** 发送缓冲区 */
static uint8_t s_tx_buf[TX_BUF_SIZE];

/** requestId 递增计数器 */
static int32_t s_request_id;

/** 是否已订阅实时数值 */
static int32_t s_subscribed;

/** 上次实时数值推送时间戳 */
static uint32_t s_last_current_values_tick;

/* ============================================================================
 * 内部函数前置声明
 * ============================================================================ */

static void SendResponse(int32_t function_id, int32_t request_id,
                          const void *body, int32_t body_len);

/** Heartbeat-发送心跳 */
void PushHeartbeat(void);

/** SendSystemStateInfos-发送系统状态信息 */
void PushSystemState(void);

/** SendSystemErrorInfos-发送报警信息 */
void PushSystemError(int32_t errorType, int32_t errorCode);

/** SendProgramStepResultInfos-发送程序执行步骤结果信息 */
void PushStepResult(void);

/** RspCurrentValues-当前实时数值响应 */
void PushCurrentValues(void);

/* ============================================================================
 * Handler 函数实现
 * ============================================================================ */

/** RspSetBackDoorParameter-后门参数设置 */
static void handle_set_backdoor_param(const struct PacketHeader *hdr,
                                       const uint8_t *body)
{
    FDS_DEBUG("[FDS] SetBackDoorParam (fid=%d)\r\n", hdr->function_id);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(BackdoorParam_T)) 
		{
        const BackdoorParam_T *p = (const void *)body;
        memcpy(&Global_BackdoorParam, p, sizeof(BackdoorParam_T));
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SET_BACKDOOR_PARAM, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** RspSetBackDoorParameter-后门参数获取 */
static void handle_get_backdoor_param(const struct PacketHeader *hdr,
                                       const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetBackDoorParam (fid=%d)\r\n", hdr->function_id);
    {
        SendResponse(FID_RSP_GET_BACKDOOR_PARAM, hdr->request_id,
                     &Global_BackdoorParam, sizeof(Global_BackdoorParam));
    }
}

/** RspSetSystemParameter-系统参数设置 */
static void handle_set_system_param(const struct PacketHeader *hdr,
                                     const uint8_t *body)
{
    FDS_DEBUG("[FDS] SetSystemParam (fid=%d)\r\n", hdr->function_id);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(SystemParam_T)) 
		{
        const SystemParam_T *p = (const void *)body;
        memcpy(&Global_SystemParam, p, sizeof(SystemParam_T));
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SET_SYSTEM_PARAM, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** RspSetSystemParameter-系统参数获取 */
static void handle_get_system_param(const struct PacketHeader *hdr,
                                     const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetSystemParam (fid=%d)\r\n", hdr->function_id);
    {
        SendResponse(FID_RSP_GET_SYSTEM_PARAM, hdr->request_id,
                     &Global_SystemParam, sizeof(Global_SystemParam));
    }
}

/** RspSetOperationalMaintenanceInfo-运维信息设置 */
static void handle_set_opmaint(const struct PacketHeader *hdr,
                                const uint8_t *body)
{
    FDS_DEBUG("[FDS] SetOpmaintInfo (fid=%d)\r\n", hdr->function_id);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(OpmaintSet_T)) 
		{
        const OpmaintSet_T *p = (const void *)body;
        if (p->dataType == 1) 
				{
            Global_OpmaintInfo.suggestedReplaceBelt     = 0;
            Global_OpmaintInfo.accumulatedRunningTimeBelt = 0;
            FDS_DEBUG("[FDS] Clear belt counters\r\n");
        } else if (p->dataType == 2) 
				{
            Global_OpmaintInfo.suggestedReplaceLube     = 0;
            Global_OpmaintInfo.accumulatedRunningTimeLube = 0;
            FDS_DEBUG("[FDS] Clear lube counters\r\n");
        }
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SET_OPMAINT_INFO, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** RspSetOperationalMaintenanceInfo-运维信息获取 */
static void handle_get_opmaint(const struct PacketHeader *hdr,
                                const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetOpmaintInfo (fid=%d)\r\n", hdr->function_id);
    (void)body;
    {
        SendResponse(FID_RSP_GET_OPMAINT_INFO, hdr->request_id,
                     &Global_OpmaintInfo, sizeof(Global_OpmaintInfo));
    }
}

/** RspSaveProgramInfo-程序保存 */
static void handle_save_program(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{
    FDS_DEBUG("[FDS] SaveProgram (len=%d)\r\n", hdr->body_length);
    if (body != NULL && hdr->body_length > 0) 
		{
        int32_t copy_len = hdr->body_length;
        if (copy_len > (int32_t)sizeof(Global_ProgramInfo)) 
				{
            copy_len = (int32_t)sizeof(Global_ProgramInfo);
        }
        memset(Global_ProgramInfo, 0, sizeof(Global_ProgramInfo));
        memcpy(Global_ProgramInfo, body, copy_len);
        FDS_DEBUG("[FDS] Program saved (%d bytes)\r\n", copy_len);
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SAVE_PROGRAM, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** RspGetProgramListInfos-程序列表信息获取 */
static void handle_get_program_list(const struct PacketHeader *hdr,
                                     const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetProgramList\r\n");
    (void)body;
    SendResponse(FID_RSP_GET_PROGRAM_LIST, hdr->request_id,
                 &Global_ProgramList, sizeof(Global_ProgramList));
}

/** RspGetProgram-程序信息获取 */
static void handle_get_program(const struct PacketHeader *hdr,
                                const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetProgram\r\n");
    (void)body;
    SendResponse(FID_RSP_GET_PROGRAM, hdr->request_id,
                 Global_ProgramInfo, sizeof(Global_ProgramInfo));
}

/** ReqGetIOStateInfos-IO 状态信息获取 */
static void handle_get_io_state(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetIOState (fid=%d)\r\n", hdr->function_id);
    {
        IoState_T rsp;
        memset(&rsp, 0, sizeof(rsp));
        SendResponse(FID_RSP_GET_IO_STATE, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** RspSynchronizeTime-时间同步 */
static void handle_sync_time(const struct PacketHeader *hdr,
                              const uint8_t *body)
{
    FDS_DEBUG("[FDS] SyncTime (fid=%d)\r\n", hdr->function_id);

    if (body != NULL && hdr->body_length >= (int32_t)sizeof(Datetime_T)) 
		{
        memcpy(&Global_DateTime, body, sizeof(Datetime_T));
    }

    {
        /* 响应结构：result(4B) + Datetime_T(24B) */
        uint8_t rsp_buf[sizeof(int32_t) + sizeof(Datetime_T)];
        *(int32_t *)rsp_buf = 1;  /* result = success */
        memcpy(rsp_buf + sizeof(int32_t), &Global_DateTime, sizeof(Datetime_T));
        SendResponse(FID_RSP_SYNC_TIME, hdr->request_id,
                     rsp_buf, sizeof(rsp_buf));
    }
}

/** RspGetSystemDatetime-系统时间获取 */
static void handle_get_datetime(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetDatetime\r\n");
    {
        SendResponse(FID_RSP_GET_DATETIME, hdr->request_id,
                     &Global_DateTime, sizeof(Global_DateTime));
    }
}

/** RspGetCurvePointDatas-曲线数据获取 */
static void handle_get_curve(const struct PacketHeader *hdr,
                              const uint8_t *body)
{
    FDS_DEBUG("[FDS] GetCurvePoints (index=%ld)\r\n",
              body ? *(const int32_t *)body : -1);
    {
        SendResponse(FID_RSP_GET_CURVE_POINTS, hdr->request_id,
                     &Global_CurvePointData, sizeof(Global_CurvePointData));
    }
}

/** RspControlServoTurn-控制电机旋转 */
static void handle_servo_turn(const struct PacketHeader *hdr,
                               const uint8_t *body)
{
    FDS_DEBUG("[FDS] ServoTurn\r\n");
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(ServoTurn_T)) 
		{
        const ServoTurn_T *s = (const void *)body;
        (void)s;
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SERVO_TURN, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** RspControlServoStop-控制电机停止 */
static void handle_servo_stop(const struct PacketHeader *hdr,
                               const uint8_t *body)
{
    FDS_DEBUG("[FDS] ServoStop\r\n");
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(FID_RSP_SERVO_STOP, hdr->request_id,
                     &rsp, sizeof(rsp));
    }
}

/** 气缸控制适配器wrapper */
static void handle_cylinder_ctrl(const struct PacketHeader *hdr,
                                  const uint8_t *body,
                                  int32_t rsp_fid,
                                  const char *name)
{
    FDS_DEBUG("[FDS] %s\r\n", name);
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(int32_t)) 
		{
				#if ETH_FDS_DEBUG
						int32_t direction = *(const int32_t *)body;
						FDS_DEBUG(" direction=%ld\r\n", direction);
				#endif
        if (hdr->body_length >= (int32_t)sizeof(CylinderCtrl_T)) {
            const CylinderCtrl_T *c = (const void *)body;
            (void)c;
        }
    }
    {
        int32_t result = 1;
        Result_T rsp = {result};
        SendResponse(rsp_fid, hdr->request_id, &rsp, sizeof(rsp));
    }
}
/** 把具体气缸 ID（148/NOK、150/螺丝刀、152/钳口）映射到通用处理函数 */
/** ReqControlNOKCylinder-nok气缸控制 */
static void handle_nok_cylinder(const struct PacketHeader *hdr,
                                 const uint8_t *body)
{ handle_cylinder_ctrl(hdr, body, FID_RSP_NOK_CYLINDER, "NOKCylinder"); }

/** RspControlScrewdriverCylinder-螺丝刀气缸控制 */
static void handle_screwdriver_cylinder(const struct PacketHeader *hdr,
                                         const uint8_t *body)
{ handle_cylinder_ctrl(hdr, body, FID_RSP_SCREWDRIVER_CYLINDER, "ScrewdriverCylinder"); }

/** ReqControlFeedStrokeCylinder-钳口气缸控制 */
static void handle_feedstroke_cylinder(const struct PacketHeader *hdr,
                                        const uint8_t *body)
{ handle_cylinder_ctrl(hdr, body, FID_RSP_FEEDSTROKE_CYLINDER, "FeedStrokeCylinder"); }

/** ReqSubCurrentValues-订阅当前实时数值 */
static void handle_sub_current_values(const struct PacketHeader *hdr,
                                       const uint8_t *body)
{
    if (body != NULL && hdr->body_length >= (int32_t)sizeof(int32_t)) {
        int32_t subType = *(const int32_t *)body;
        s_subscribed = (subType == 1) ? 1 : 0;
        FDS_DEBUG("[FDS] SubCurrentValues subType=%ld subscribed=%d\r\n",
                  subType, s_subscribed);
    }
}
/* ============================================================================
 * 推送函数实现
 * ============================================================================ */
/** Heartbeat-心跳 */
void PushHeartbeat(void)
{
    Heartbeat_T body = {(int32_t)HAL_GetTick()};
    SendResponse(FID_HEARTBEAT, 0, &body, sizeof(body));
}

/** 推送系统状态 */
void PushSystemState(void)
{
    SysStatePush_T state;
    state.logicLock    = Global_LogicLock;
    state.systemModel  = Global_SystemModel;
    state.readyState   = Global_ReadyState;
    state.systemState  = Global_SystemState;
    SendResponse(FID_SEND_SYSTEM_STATE, 0, &state, sizeof(state));
}

/** 推送报警信息 */
void PushSystemError(int32_t errorType, int32_t errorCode)
{
    SysError_T err;
    err.errorType = errorType;
    err.errorCode = errorCode;
    SendResponse(FID_SEND_SYSTEM_ERROR, 0, &err, sizeof(err));
}

/** 推送程序执行步骤结果 */
void PushStepResult(void)
{
    SendResponse(FID_SEND_STEP_RESULT, 0, Global_ProgramStepResultInfos,
                 sizeof(Global_ProgramStepResultInfos));
}

/** 推送实时数值 */
void PushCurrentValues(void)
{
    CurrentValues_T vals;
    memset(&vals, 0, sizeof(vals));
    /* TODO: 读取实际硬件值 */
    vals.servoRpmSpeed     = 100;
    vals.servoTorque       = 200;
    vals.servoAngle        = 300;
    vals.cylinderPosition  = 400;
    SendResponse(FID_RSP_CURRENT_VALUES, 0, &vals, sizeof(vals));
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
    uint8_t sn = HANDLER_SOCKET;

    total_len = ProtocolEncode(s_tx_buf, function_id, request_id,
                               body, body_len);
    if (total_len > 0) 
		{
        uint8_t status = getSn_SR(sn);
        if (status == SOCK_ESTABLISHED) 
				{
            wiz_send(sn, s_tx_buf, (uint16_t)total_len);
        }
    }
}

/**
 * @brief  尝试从 TCP Server socket 接收数据并分发
 *
 * 使用读写指针管理缓冲区，避免频繁 memmove。
 */
static void RecvAndDispatch(void)
{
    int32_t recv_len;
    int32_t data_len;
    int32_t total_len;
    struct PacketHeader hdr;

    /*
     * 循环读取：W6100 可能将 TCP 数据分多次交付（每次 ~37 字节），
     * 必须一次性读完所有可用数据，避免缓冲区残留导致后续包无法拼完整。
     */
    for (;;) {
        /* 缓冲区尾部空间不足时，将未处理数据移到头部 */
        if (s_rx_wr + PACKET_HEADER_SIZE > RX_BUF_SIZE) 
				{
            int32_t remaining = s_rx_wr - s_rx_rd;
            if (remaining > 0 && remaining < (int32_t)sizeof(s_rx_buf)) 
						{
                memmove(s_rx_buf, s_rx_buf + s_rx_rd, remaining);
            }
            s_rx_rd = 0;
            s_rx_wr = remaining;
        }

        if (s_rx_wr >= RX_BUF_SIZE) break;

        recv_len = wiz_recv(HANDLER_SOCKET, s_rx_buf + s_rx_wr,
                            RX_BUF_SIZE - s_rx_wr);
        if (recv_len <= 0) break;

        s_rx_wr += recv_len;
    }

    data_len = s_rx_wr - s_rx_rd;
    if (data_len < PACKET_HEADER_SIZE) return;

    /* 循环解析缓冲区中的所有完整报文 */
    while (data_len >= PACKET_HEADER_SIZE) 
		{
        if (ProtocolDecodeHeader(s_rx_buf + s_rx_rd, data_len, &hdr) != 0) 
				{
            s_rx_rd++;
            data_len--;
            continue;
        }

        total_len = hdr.packet_length;
        if (data_len < total_len) 
				{
            /* 头部声明的包长大于实际可用数据：TCP 分包，数据未收齐，等下次调度 */
            break;
        }

        FDS_DEBUG("[FDS] Dispatch FID=%ld (len=%ld)\r\n",
                  hdr.function_id, total_len);

        ProtocolDispatch(s_rx_buf + s_rx_rd, total_len,
                         s_dispatch_table, s_dispatch_table_size);

        s_rx_rd += total_len;
        data_len -= total_len;
    }

    /* 全部处理完则重置指针 */
    if (s_rx_rd >= s_rx_wr) 
		{
        s_rx_rd = 0;
        s_rx_wr = 0;
    }
}

/* ============================================================================
 * 外部接口实现
 * ============================================================================ */

void HandlerInit(void)
{
    DataInit();

    s_rx_rd = 0;
    s_rx_wr = 0;
    s_request_id = 0;
    (void)s_request_id;
    s_subscribed           = 0;
    s_last_current_values_tick = 0;

    wiz_socket(HANDLER_SOCKET, Sn_MR_TCP, HANDLER_PORT, SF_IO_NONBLOCK | SF_TCP_NODELAY);
    wiz_listen(HANDLER_SOCKET);

    FDS_DEBUG("[FDS] Handler initialized, TCP Server listening on port %d\r\n",
              HANDLER_PORT);


}

void HandlerTask(void)
{
    uint8_t status;
    static uint8_t s_prev_status = 0xFF;  /* 上次 socket 状态，用于防重复日志刷屏 */

    status = getSn_SR(HANDLER_SOCKET);

    if (status == SOCK_ESTABLISHED) 
		{
        RecvAndDispatch();
    }

    /* 已订阅时，1s 周期性推送实时数值 */
    if (s_subscribed && status == SOCK_ESTABLISHED) {
        if (HAL_GetTick() - s_last_current_values_tick >= 1000) {
            s_last_current_values_tick = HAL_GetTick();
            PushCurrentValues();
        }
    }

//    /* 测试：2s 周期性推送步骤结果 */
//    if (status == SOCK_ESTABLISHED) {
//        static uint32_t s_last_test_push_tick = 0;
//        if (HAL_GetTick() - s_last_test_push_tick >= 2000) {
//            s_last_test_push_tick = HAL_GetTick();
//            PushStepResult();
//        }
//    }

    if (status == SOCK_CLOSE_WAIT) 
		{
        if (s_prev_status != SOCK_CLOSE_WAIT) {
            wiz_close(HANDLER_SOCKET);
            wiz_socket(HANDLER_SOCKET, Sn_MR_TCP, HANDLER_PORT, SF_IO_NONBLOCK | SF_TCP_NODELAY);
            wiz_listen(HANDLER_SOCKET);
            s_rx_rd = 0;
            s_rx_wr = 0;
            FDS_DEBUG("[FDS] Client disconnected, re-listening\r\n");
        }
    } else if (status == SOCK_CLOSED) 
		{
        if (s_prev_status != SOCK_CLOSED) {
            wiz_socket(HANDLER_SOCKET, Sn_MR_TCP, HANDLER_PORT, SF_IO_NONBLOCK | SF_TCP_NODELAY);
            wiz_listen(HANDLER_SOCKET);
            s_rx_rd = 0;
            s_rx_wr = 0;
            FDS_DEBUG("[FDS] Socket reopened, listening\r\n");
        }
    }

    s_prev_status = status;
}
