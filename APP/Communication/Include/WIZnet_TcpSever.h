/**
 * @file    WIZnet_TcpSever.h
 * @brief   WIZnet TCP 服务器——发送当前 Tick 时间戳
 */
#ifndef WIZNET_TCPSERVER_H
#define WIZNET_TCPSERVER_H

#include <stdint.h>

int  WIZnet_TcpSever_init(void);
void WIZnet_TcpSever_task(void);

#endif /* WIZNET_TCPSERVER_H */
