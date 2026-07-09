/**
 * @file    ETH_WIZdemo.c
 * @brief   以太网初始化示例（支持 W6300 QSPI / W6100 SPI）
 *
 * 通过条件编译 _WIZCHIP_ 宏选择芯片：
 *   #define _WIZCHIP_  W6300  -> 使用 QSPI 驱动 W6300
 *   #define _WIZCHIP_  W6100  -> 使用 SPI 驱动 W6100
 *
 * W6300（QSPI）引脚映射：
 *   PB2 - CLK   PF8 - IO0   PF9 - IO1   PF7 - IO2   PF6 - IO3
 *   PG6 - CS    PC4 - RST
 *
 * W6100（SPI1）引脚映射：
 *   PA5 - SCK   PA6 - MISO   PA7 - MOSI
 *   PA4 - CS    PC6 - RST
 */

#include "ETH_WIZdemo.h"
#include "wizchip_conf.h"
#include "bsp_spi.h"
#include "spi_w6100.h"
#include <string.h>

/* 网络参数（与参考例程一致，含 IPv6） */
/* MAC: 00:08:DC:11:22:33 -> EUI-64 LLA: FE80::0208:DCFF:FE11:2233 */
static wiz_NetInfo g_netinfo = {
    .mac = {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33},
    .ip  = {192, 168, 1, 2},
    .sn  = {255, 255, 255, 0},
    .gw  = {192, 168, 0, 254},
    .dns = {8, 8, 8, 8},
    .ipmode = NETINFO_STATIC_ALL,
    .lla = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x08, 0xdc, 0xff, 0xfe, 0x11, 0x22, 0x33},
    .gua = {0x20, 0x01, 0x02, 0xb8, 0x00, 0x10, 0x00, 0x01,
            0x02, 0x08, 0xdc, 0xff, 0xfe, 0x11, 0x22, 0x33},
    .sn6 = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .gw6 = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x87, 0xff, 0xfe, 0x08, 0x4c, 0x81},
};

/* ============================================================================
 * 全局：WIZnet 远端目标 IP（各 TCP/UDP 应用文件读取此处连接远端）
 * ============================================================================ */
uint8_t g_wiz_remote_ip[4] = {192, 168, 1, 134};

/* ============================================================================
 * W6100 初始化（SPI1）
 * ============================================================================ */

int ETH_WIZdemo_init(void)
{
    int ret;
    uint8_t buf_size[2][8] = {{8,4,4,0,0,0,0,0},{2,2,2,2,2,2,2,2}};
    uint8_t temp;

    ETH_DEBUG("[ETH] W6100 init...\r\n");

    /* 1. SPI1 初始化 */
    ETH_DEBUG("  SPI1 init...\r\n");
    ret = w6100_spi_init(NULL);
    if (ret != 0) return -1;
    ETH_DEBUG("  SPI1 OK\r\n");

    /* 2. 硬件复位 */
    w6100_hw_reset();
    ETH_DEBUG("  W6100 ready\r\n");

    /* 3. 注册 ioLibrary 回调
     * 注意：burst 回调传 NULL，ioLibrary 会使用默认的逐字节实现（调用 read_byte/write_byte），
     * 避免自定义 burst 在 W6100 VDM 模式下可能产生的时序问题。
     */
    reg_wizchip_cs_cbfunc(w6100_cs_sel, w6100_cs_desel);
    reg_wizchip_spi_cbfunc(w6100_spi_read_byte, w6100_spi_write_byte,
                           NULL, NULL);

    /* 4. 初始化芯片 */
    temp = IK_DEST_UNREACH;
    ctlwizchip(CW_SET_INTRMASK, &temp);
    if (ctlwizchip(CW_INIT_WIZCHIP, buf_size) == -1) {
        ETH_DEBUG("  W6100 init FAILED\r\n");
        return -2;
    }
    ETH_DEBUG("  W6100 init OK\r\n");

    /* 5. 等待 PHY 链接（软件复位后才可检测） */
    ETH_DEBUG("  Waiting PHY link...\r\n");
    {
        uint32_t phy_wait = HAL_GetTick();
        do {
            if (ctlwizchip(CW_GET_PHYLINK, &temp) == -1) {
                ETH_DEBUG("  Unknown PHY link status\r\n");
                break;
            }
            ETH_DEBUG("  PHY link raw = %d\r\n", temp);
            HAL_Delay(1000);
        } while ((temp == 0) && (HAL_GetTick() - phy_wait < 5000));
    }
    if (temp != 0)
        ETH_DEBUG("  PHY link OK\r\n");
    else
        ETH_DEBUG("  PHY link timeout, continuing anyway\r\n");

    /* 6. 寄存器诊断 */
    ETH_DEBUG("  CHIP ID = 0x%04x\r\n", getCIDR());
    ETH_DEBUG("  VERSION = 0x%04x\r\n", getVER());

    /* 7. 配置网络参数 */
    ctlnetwork(CN_SET_NETINFO, &g_netinfo);

    /* 验证 MAC */
    {
        uint8_t mac_rd[6];
        getSHAR(mac_rd);
        ETH_DEBUG("  MAC=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
               mac_rd[0], mac_rd[1], mac_rd[2],
               mac_rd[3], mac_rd[4], mac_rd[5]);
    }

    ETH_DEBUG("[ETH] W6100 ready: %d.%d.%d.%d\r\n",
           g_netinfo.ip[0], g_netinfo.ip[1],
           g_netinfo.ip[2], g_netinfo.ip[3]);
    return 0;
}



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
 * @endcode
 */
void wiz_set_ip(const uint8_t ip[4], const uint8_t sn[4], const uint8_t gw[4])
{
    memcpy(g_netinfo.ip, ip, 4);
    memcpy(g_netinfo.sn, sn, 4);
    memcpy(g_netinfo.gw, gw, 4);
    ctlnetwork(CN_SET_NETINFO, &g_netinfo);

    ETH_DEBUG("[ETH] WIZ IP changed to %d.%d.%d.%d\r\n",
              ip[0], ip[1], ip[2], ip[3]);
}

/**
 * @brief  在线更改 WIZnet 远端目标 IP
 * @param  ip   远端 IP 地址（4 字节数组），如 {192,168,1,200}
 * @note   各 TCP/UDP 应用文件读取 g_wiz_remote_ip 连接远端，
 *         更改后需等待自动重连。
 * @code
 *   wiz_set_remote_ip((uint8_t[]){192,168,1,200});
 * @endcode
 */
void wiz_set_remote_ip(const uint8_t ip[4])
{
    g_wiz_remote_ip[0] = ip[0];
    g_wiz_remote_ip[1] = ip[1];
    g_wiz_remote_ip[2] = ip[2];
    g_wiz_remote_ip[3] = ip[3];
}

void ETH_WIZdemo_task(void)
{
    /* 链路状态监测：检测网线插入/拔出状态变化 */
    static uint8_t s_prev_link = 0xFF;  /* 0xFF = 初始未知状态 */
    static uint32_t s_last_tick = 0;
    uint8_t link;
    uint32_t now = HAL_GetTick();

    /* 每 500ms 检查一次 */
    if (now - s_last_tick < 500) return;
    s_last_tick = now;

    if (ctlwizchip(CW_GET_PHYLINK, &link) == -1) return;

    if (link != s_prev_link) {
        if (link == PHY_LINK_ON) {
            ETH_DEBUG("[ETH] W6100 Link UP\r\n");
        } else {
            ETH_DEBUG("[ETH] W6100 Link DOWN\r\n");
        }
        s_prev_link = link;
    }
}
