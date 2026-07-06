/**
 * @file    WIZnet_UDP.c
 * @brief   WIZnet UDP 通信——发送当前 Tick 时间戳
 *
 * WIZnet 硬件 UDP 堆栈实现，向远端（默认 192.168.1.134:6002）
 * 发送 4 字节 HAL_GetTick()。
 * 远端 IP 可通过串口指令 $Test,NET,WIZ_REMOTE,<IP># 在线修改。
 */
#include "WIZnet_UDP.h"
#include "ETH_WIZdemo.h"
#include "wizchip_conf.h"
#include "wiz_socket.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

#define WIZ_UDP_PORT            6002
#define WIZ_UDP_SOCKET          2

static uint8_t s_init_done = 0;

int WIZnet_UDP_init(void)
{
    int8_t ret;

    ret = wiz_socket(WIZ_UDP_SOCKET, Sn_MR_UDP, WIZ_UDP_PORT, 0);
    if (ret != WIZ_UDP_SOCKET) {
        ETH_DEBUG("[WIZ_UDP] socket(%u) failed: %d\r\n", WIZ_UDP_SOCKET, ret);
        return -1;
    }

    {
        uint8_t nb = SOCK_IO_NONBLOCK;
        wiz_ctlsocket(WIZ_UDP_SOCKET, CS_SET_IOMODE, &nb);
    }

    s_init_done = 1;
    ETH_DEBUG("[WIZ_UDP] Ready, socket %u port %u\r\n", WIZ_UDP_SOCKET, WIZ_UDP_PORT);
    return 0;
}

void WIZnet_UDP_task(void)
{
    if (!s_init_done) return;

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "TICK:%lu\r\n", HAL_GetTick());
    if (len > 0) {
        wiz_sendto(WIZ_UDP_SOCKET, (uint8_t *)buf, (uint16_t)len, g_wiz_remote_ip, WIZ_UDP_PORT, 4);
    }
}

