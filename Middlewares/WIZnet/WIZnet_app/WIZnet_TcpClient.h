/**
 * @file    WIZnet_TcpClient.h
 * @brief   WIZnet TCP 客户端——发送当前 Tick 时间戳
 */
#ifndef WIZNET_TCPCLIENT_H
#define WIZNET_TCPCLIENT_H

#include <stdint.h>

int  WIZnet_TcpClient_init(void);
void WIZnet_TcpClient_task(void);

#endif /* WIZNET_TCPCLIENT_H */
