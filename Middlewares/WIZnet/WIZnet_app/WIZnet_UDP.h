/**
 * @file    WIZnet_UDP.h
 * @brief   WIZnet UDP 通信——发送当前 Tick 时间戳
 */
#ifndef WIZNET_UDP_H
#define WIZNET_UDP_H

#include <stdint.h>

int  WIZnet_UDP_init(void);
void WIZnet_UDP_task(void);

#endif /* WIZNET_UDP_H */
