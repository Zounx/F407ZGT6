/**
 * @file    WIZnet_TcpSever.c
 * @brief   WIZnet TCP 服务器——发送当前 Tick 时间戳
 *
 * WIZnet 硬件 TCP 堆栈实现，监听端口 5002，
 * 接收客户端连接并发送 4 字节 HAL_GetTick()。
 * 由调度器周期性调用 task() 发送。
 */
#include "WIZnet_TcpSever.h"
#include "ETH_WIZdemo.h"
#include "wizchip_conf.h"
#include "wiz_socket.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

#define WIZ_TCPSERVER_PORT      5002
#define WIZ_SOCKET              0
static uint8_t s_init_done = 0;

int WIZnet_TcpSever_init(void)
{
    int8_t ret;

    ret = wiz_socket(WIZ_SOCKET, Sn_MR_TCP, WIZ_TCPSERVER_PORT, 0);
    if (ret != WIZ_SOCKET) 
		{
        ETH_DEBUG("[WIZ_TCPS] socket(%u) failed: %d\r\n", WIZ_SOCKET, ret);
        return -1;
    }

    ret = wiz_listen(WIZ_SOCKET);
    if (ret != SOCK_OK) 
		{
        ETH_DEBUG("[WIZ_TCPS] listen failed: %d\r\n", ret);
        return -1;
    }

    s_init_done = 1;
    ETH_DEBUG("[WIZ_TCPS] Listening on port %u, socket %u\r\n", WIZ_TCPSERVER_PORT, WIZ_SOCKET);
    return 0;
}

void WIZnet_TcpSever_task(void)
{
    if (!s_init_done) return;

    uint8_t status = getSn_SR(WIZ_SOCKET);

    if (status == SOCK_ESTABLISHED) 
		{
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "TICK:%lu\r\n", HAL_GetTick());
        if (len > 0) 
				{
            {
                uint8_t nb = SOCK_IO_NONBLOCK;
                wiz_ctlsocket(WIZ_SOCKET, CS_SET_IOMODE, &nb);
            }
            wiz_send(WIZ_SOCKET, (uint8_t *)buf, (uint16_t)len);
        }
    } else if (status == SOCK_CLOSE_WAIT) 
		{
        /* 客户端发起断开，关闭当前 socket 并重新监听 */
        wiz_close(WIZ_SOCKET);
        wiz_socket(WIZ_SOCKET, Sn_MR_TCP, WIZ_TCPSERVER_PORT, SF_IO_NONBLOCK);
        wiz_listen(WIZ_SOCKET);
        ETH_DEBUG("[WIZ_TCPS] Client disconnected, re-listening\r\n");
    }
}
