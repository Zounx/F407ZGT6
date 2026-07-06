/**
 * @file    ETH_WIZdemo.h
 * @brief   以太网初始化演示（支持 W6300 QSPI / W6100 SPI）
 *
 * 通过 _WIZCHIP_ 宏在 wizchip_conf.h 中选择芯片：
 *   #define _WIZCHIP_  W6300  -> QSPI（引脚见 ETH_WIZdemo.c）
 *   #define _WIZCHIP_  W6100  -> SPI1（引脚见 ETH_WIZdemo.c）
 *
 * ============================================================================
 *  快速开始
 * ============================================================================
 *
 * // 1. 在 main 中调用
 * ETH_WIZdemo_init();
 *
 * // 2. 在主循环中调用
 * ETH_WIZdemo_task();
 */

#ifndef ETH_WIZDEMO_H
#define ETH_WIZDEMO_H

#include <stdint.h>
#include "bsp_usart.h"

/* USART1 对象（由 Main.c 定义） */
extern struct bsp_usart s_usart1;

/* ============================================================================
 * 调试开关
 *
 * ETH_WIZDEMO_DEBUG 设为 1 时 ETH_DEBUG() 通过 USART1 打印调试信息；
 * 设为 0 时所有 ETH_DEBUG 被编译为空，不产生任何代码。
 * ============================================================================ */

#define ETH_WIZDEMO_DEBUG  1       /* 调试开关：1=开启，0=关闭 */

#if ETH_WIZDEMO_DEBUG
    #define ETH_DEBUG(...)  bsp_usart_printf(&s_usart1, __VA_ARGS__)
#else
    #define ETH_DEBUG(...)  ((void)0)
#endif



/**
 * @brief  初始化 WIZnet 以太网芯片
 *
 * 根据 _WIZCHIP_ 宏选择初始化方式：
 *   - W6300：通过 bsp_quadspi + QSPI 间接模式
 *   - W6100：通过 bsp_spi + SPI1 全双工模式
 *
 * 完成以下工作：
 *   1. 初始化底层接口（QSPI/SPI）
 *   2. 硬件复位芯片
 *   3. 注册 ioLibrary 回调
 *   4. 等待 PHY 连接（超时 5s）
 *   5. 调用 ctlwizchip(CW_INIT_WIZCHIP) 初始化
 *   6. 设置网络参数（IP/MAC/网关/掩码）
 *
 * @return 0  成功
 * @return -1 底层接口初始化失败
 * @return -2 芯片初始化失败
 */
int  ETH_WIZdemo_init(void);

/**
 * @brief  周期任务（主循环中调用）
 *
 * 当前为占位，后续根据应用需要填充。
 */
void ETH_WIZdemo_task(void);

/**
 * @brief  在线更改 WIZnet IP 参数
 * @param  ip   本机 IP（4 字节）
 * @param  sn   子网掩码（4 字节）
 * @param  gw   默认网关（4 字节）
 * @code
 *   uint8_t ip[4] = {192, 168, 0, 50};
 *   uint8_t sn[4] = {255, 255, 255, 0};
 *   uint8_t gw[4] = {192, 168, 0, 254};
 *   wiz_set_ip(ip, sn, gw);
 *   // 或直接写字面量：
 *   // wiz_set_ip((uint8_t[]){192,168,0,50},
 *   //            (uint8_t[]){255,255,255,0},
 *   //            (uint8_t[]){192,168,0,254});
 * @endcode
 */
void wiz_set_ip(const uint8_t ip[4], const uint8_t sn[4], const uint8_t gw[4]);

/**
 * @brief  在线更改 WIZnet 远端目标 IP
 * @param  ip   远端 IP（4 字节），如 {192,168,1,134}
 * @code
 *   wiz_set_remote_ip((uint8_t[]){192,168,1,200});
 * @endcode
 */
void wiz_set_remote_ip(const uint8_t ip[4]);

/**
 * @brief  WIZnet 远端 IP 全局变量（应用模块读取此处连接目标）
 */
extern uint8_t g_wiz_remote_ip[4];

#endif /* ETH_WIZDEMO_H */
