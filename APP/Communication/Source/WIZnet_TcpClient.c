/**
 * @file    WIZnet_TcpClient.c
 * @brief   WIZnet TCP 客户端——发送当前 Tick 时间戳
 *
 * WIZnet 硬件 TCP 堆栈实现，连接远端（默认 192.168.1.134:5002），
 * 周期性发送 4 字节 HAL_GetTick()。
 * 远端 IP 可通过串口指令 $Test,NET,WIZ_REMOTE,<IP># 在线修改，自动重连。
 */
#include "WIZnet_TcpClient.h"
#include "ETH_WIZdemo.h"
#include "wizchip_conf.h"
#include "wiz_socket.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

#define WIZ_TCPCLIENT_REMOTE_PORT   5002
#define WIZ_TCPCLIENT_SOCKET        1

static uint8_t s_connected = 0;
static uint8_t s_connecting = 0;
static uint8_t s_init_done = 0;

int WIZnet_TcpClient_init(void)
{
    int8_t ret;

    ret = wiz_socket(WIZ_TCPCLIENT_SOCKET, Sn_MR_TCP, 0, SF_IO_NONBLOCK);
    if (ret != WIZ_TCPCLIENT_SOCKET) {
        ETH_DEBUG("[WIZ_TCPC] socket(%u) failed: %d\r\n", WIZ_TCPCLIENT_SOCKET, ret);
        return -1;
    }

    ret = connect(WIZ_TCPCLIENT_SOCKET, g_wiz_remote_ip, WIZ_TCPCLIENT_REMOTE_PORT, 4);
    if ((ret == SOCK_OK) || (ret == SOCK_BUSY)) {
        s_connecting = 1;
    }

    s_init_done = 1;
    ETH_DEBUG("[WIZ_TCPC] Connecting to %u.%u.%u.%u:%u (socket %u)\r\n",
           g_wiz_remote_ip[0], g_wiz_remote_ip[1],
           g_wiz_remote_ip[2], g_wiz_remote_ip[3],
           WIZ_TCPCLIENT_REMOTE_PORT, WIZ_TCPCLIENT_SOCKET);
    return 0;
}

void WIZnet_TcpClient_task(void)
{
    uint8_t status;

    if (!s_init_done) return;

    if (s_connected) {
        /* 已连接：检查连接是否仍然正常 */
        status = getSn_SR(WIZ_TCPCLIENT_SOCKET);
        if (status != SOCK_ESTABLISHED) {
            s_connected = 0;
            s_connecting = 0;
            ETH_DEBUG("[WIZ_TCPC] Disconnected, will retry\r\n");
        } else {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "TICK:%lu\r\n", HAL_GetTick());
            if (len > 0) {
                wiz_send(WIZ_TCPCLIENT_SOCKET, (uint8_t *)buf, (uint16_t)len);
            }
        }
        return;
    }

    if (s_connecting) {
        /* 连接中：检查三次握手是否完成 */
        status = getSn_SR(WIZ_TCPCLIENT_SOCKET);
        if (status == SOCK_ESTABLISHED) {
            s_connected = 1;
            s_connecting = 0;
            ETH_DEBUG("[WIZ_TCPC] Connected\r\n");
        } else if ((status == SOCK_CLOSED) || (status == SOCK_TIME_WAIT)) {
            /* 连接失败，开始重试 */
            s_connecting = 0;
        }
        /* 其他中间状态（SOCK_SYNSENT、SOCK_INIT）继续等待，不关闭 */
        return;
    }

    /* 完全断开 → 尝试重连 */
    {
        int8_t ret;
        wiz_close(WIZ_TCPCLIENT_SOCKET);
        wiz_socket(WIZ_TCPCLIENT_SOCKET, Sn_MR_TCP, 0, SF_IO_NONBLOCK);
        ret = connect(WIZ_TCPCLIENT_SOCKET, g_wiz_remote_ip, WIZ_TCPCLIENT_REMOTE_PORT, 4);
        if ((ret == SOCK_OK) || (ret == SOCK_BUSY)) {
            s_connecting = 1;
        } else {
            ETH_DEBUG("[WIZ_TCPC] reconnect fail: %d\r\n", ret);
        }
    }
}
