/**
 ****************************************************************************************************
 * @file        ExtHardwareTest.c
 * @brief       外部硬件测试 — 串口协议命令解析与分发（F407 版）
 * @note        协议格式：$Test,CMD,SUB[,参数]#，大小写不敏感
 *
 *   已支持命令：
 *     $Test,GPIO,<引脚>,<值>#      — GPIO 输出控制（如 A1,1  PA1 高电平）
 *     $Test,SRAM,MarchC#           — 外部 SRAM March C- 全扫描
 *     $Test,SD,MOUNT#              — 初始化 SD + 挂载 FATFS + 打印卡信息
 *     $Test,SD,WRITE,路径,内容#     — 写入内容到指定文件
 *     $Test,SD,READ,路径#           — 读取指定文件并串口输出
 *     $Test,SD,MKDIR,目录名#        — 创建目录
 *     $Test,SD,MKFILE,文件名#       — 创建空文件
 *     $Test,SD,LIST#               — 列出根目录
 *     $Test,SD,DELETE,文件名#       — 删除文件/空目录
 *     $Test,SD,PURE#                 — 纯函数功能全面测试（写→读→子目录→删除）
 *     $Test,SD,STRESS#               — DMA 写读压力测试（默认 10000 轮）
 *     $Test,SD,STRESS,5000#           — 指定 5000 轮
 *     $Test,NET,WIZnet,192.168.0.23#  — 设置 WIZnet 本机 IP
 *     $Test,NET,WIZ_REMOTE,192.168.1.200#  — 设置 WIZnet 远端目标 IP
 * 
 *   SD 指令使用示例（按顺序操作）：
 *     $Test,SD,MOUNT#                      —① 挂载
 *     $Test,SD,MKDIR,myfolder#             —② 创建子目录
 *     $Test,SD,WRITE,test.txt,Hello#       —③ 写根目录文件
 *     $Test,SD,WRITE,myfolder/data.txt,OK# —④ 写子目录文件
 *     $Test,SD,READ,test.txt#              —⑤ 读取
 *     $Test,SD,READ,myfolder/data.txt#     —⑥ 读取子目录文件
 *     $Test,SD,LIST#                       —⑦ 列出所有文件
 *     $Test,SD,DELETE,myfolder/data.txt#   —⑧ 删子目录内文件
 *     $Test,SD,DELETE,myfolder#            —⑨ 删空目录
 ****************************************************************************************************
 */

#include "ExtHardwareTest.h"
#include "bsp_usart.h"
#include "bsp_gpio.h"
#include "bsp_sram.h"
#include "SD.h"
#include "ETH_WIZdemo.h"
#include <string.h>
#include <stdint.h>

/* 大小写不敏感字符串比较 */
static int str_icmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        int ca = *a++;
        int cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return ca - cb;
    }
    return *a - *b;
}

/* 串口引用（由 Main.c 定义） */
extern struct bsp_usart s_usart1;

/* 测试命令表结构 */
struct test_cmd_entry {
    const char *cmd;
    const char *sub;
    void (*handler)(void);
};

/* 子命令参数字符串缓存 */
char g_test_sub[32];
char g_test_param[128];


/* ============================================================================
 * GPIO 测试 — $Test,GPIO,<引脚>,<值>#
 *   引脚格式：端口字母+引脚号，如 A1、B1、C13
 *   值：0=低电平，1=高电平
 *   自动将引脚初始化为推挽输出后再写值
 *   示例：
 *     $Test,GPIO,A1,1#   — PA1 输出高电平
 *     $Test,GPIO,B1,1#   — PB1 输出高电平
 *     $Test,GPIO,A1,0#   — PA1 输出低电平
 * ============================================================================ */
static void gpio_test_set(void)
{
    GPIO_TypeDef *port = NULL;
    char port_char;
    const char *p;
    uint16_t pin_num = 0;
    uint16_t pin_mask;
    uint8_t val;

    if (g_test_sub[0] == '\0') {
        bsp_usart_printf(&s_usart1, "[GPIO] ERR: no pin specified\r\n");
        return;
    }

    /* 端口字母（转大写） */
    port_char = g_test_sub[0];
    if (port_char >= 'a' && port_char <= 'z')
        port_char -= 'a' - 'A';

    switch (port_char) {
        case 'A': port = GPIOA; break;
        case 'B': port = GPIOB; break;
        case 'C': port = GPIOC; break;
        case 'D': port = GPIOD; break;
        case 'E': port = GPIOE; break;
        case 'F': port = GPIOF; break;
        case 'G': port = GPIOG; break;
        case 'H': port = GPIOH; break;
        case 'I': port = GPIOI; break;
        default:
            bsp_usart_printf(&s_usart1, "[GPIO] ERR: unknown port %c\r\n", port_char);
            return;
    }

    /* 引脚号（支持1~2位数字） */
    p = g_test_sub + 1;
    while (*p >= '0' && *p <= '9') {
        pin_num = pin_num * 10 + (uint16_t)(*p - '0');
        p++;
    }
    if (pin_num > 15) {
        bsp_usart_printf(&s_usart1, "[GPIO] ERR: invalid pin %u\r\n", pin_num);
        return;
    }
    pin_mask = (uint16_t)(1u << pin_num);

    /* 使能 GPIO 时钟 */
    if      (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
    else if (port == GPIOI) __HAL_RCC_GPIOI_CLK_ENABLE();

    /* 配置为推挽输出 */
    {
        GPIO_InitTypeDef cfg = {0};
        cfg.Pin   = pin_mask;
        cfg.Mode  = GPIO_MODE_OUTPUT_PP;
        cfg.Pull  = GPIO_NOPULL;
        cfg.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(port, &cfg);
    }

    /* 输出值（BSRR：低16位置位，高16位复位） */
    val = (g_test_param[0] == '0') ? 0 : 1;
    if (val)
        port->BSRR = pin_mask;
    else
        port->BSRR = (uint32_t)pin_mask << 16;

    bsp_usart_printf(&s_usart1, "[GPIO] %c%u => %u (init PP output)\r\n",
                     port_char, pin_num, val);
}


/* ============================================================================
 * NET 测试 — 在线更改 WIZnet IP 地址（不持久化，掉电恢复默认值）
 *   $Test,NET,WIZnet,192.168.0.23#  -> 设置 WIZnet IP = 192.168.0.23
 *   说明：g_test_param 存放点分十进制字符串，如 "192.168.0.23"
 * ============================================================================ */

/**
 * @brief  解析点分十进制 IP 字符串 "192.168.0.23" → {192,168,0,23}
 * @return 0 成功，-1 格式错误
 */
static int parse_ip_dot(const char *str, uint8_t ip[4])
{
    uint8_t n = 0;
    uint32_t val = 0;

    while (*str && n < 4) {
        if (*str >= '0' && *str <= '9') {
            val = val * 10 + (uint32_t)(*str - '0');
        } else if (*str == '.') {
            if (val > 255) return -1;
            ip[n++] = (uint8_t)val;
            val = 0;
        } else {
            return -1;
        }
        str++;
    }
    if (val > 255) return -1;
    ip[n++] = (uint8_t)val;

    return (n == 4) ? 0 : -1;
}

static void net_set_ip_handler(void)
{
    uint8_t ip[4];

    if (g_test_sub[0] == '\0' ||
        (str_icmp(g_test_sub, "WIZnet") != 0)) {
        bsp_usart_printf(&s_usart1, "[NET] ERR: need WIZnet\r\n");
        return;
    }

    if (g_test_param[0] == '\0' || parse_ip_dot(g_test_param, ip) != 0) {
        bsp_usart_printf(&s_usart1, "[NET] ERR: bad IP '%s'\r\n", g_test_param);
        return;
    }

    {
        uint8_t sn[4] = {255, 255, 255, 0};
        uint8_t gw[4] = {192, 168, 0, 254};
        wiz_set_ip(ip, sn, gw);
        bsp_usart_printf(&s_usart1, "[NET] WIZnet => %d.%d.%d.%d\r\n",
                         ip[0], ip[1], ip[2], ip[3]);
    }
}

/**
 * @brief  在线更改 WIZnet 远端目标 IP
 *
 * 指令格式：
 *   $Test,NET,WIZ_REMOTE,192.168.1.200#
 */
static void net_set_remote_ip_handler(void)
{
    uint8_t ip[4];

    if (g_test_sub[0] == '\0' ||
        (str_icmp(g_test_sub, "WIZ_REMOTE") != 0)) {
        bsp_usart_printf(&s_usart1, "[NET] ERR: need WIZ_REMOTE\r\n");
        return;
    }

    if (g_test_param[0] == '\0' || parse_ip_dot(g_test_param, ip) != 0) {
        bsp_usart_printf(&s_usart1, "[NET] ERR: bad IP '%s'\r\n", g_test_param);
        return;
    }

    wiz_set_remote_ip(ip);
    bsp_usart_printf(&s_usart1, "[NET] WIZnet remote => %d.%d.%d.%d\r\n",
                     ip[0], ip[1], ip[2], ip[3]);
}


/* ============================================================================
 * 测试命令表
 * ============================================================================ */
static const struct test_cmd_entry s_cmd_table[] = {
    {"GPIO",  "",       gpio_test_set},    /* 空 sub = 通配，由 handler 自行解析引脚名和值 */
    {"SRAM",  "MarchC", bsp_sram_scan},
    /* SD 卡 FATFS 测试 */
    {"SD",    "MOUNT",  sd_test_mount},
    {"SD",    "WRITE",  sd_test_write},
    {"SD",    "READ",   sd_test_read},
    {"SD",    "MKDIR",  sd_test_mkdir},
    {"SD",    "MKFILE", sd_test_mkfile},
    {"SD",    "LIST",   sd_test_list},
    {"SD",    "DELETE", sd_test_delete},
    {"SD",    "PURE",   sd_test_pure},
    {"SD",    "STRESS", sd_test_stress},
    {"NET",   "WIZnet",   net_set_ip_handler},
    {"NET",   "WIZ_REMOTE", net_set_remote_ip_handler},
};

static const uint8_t s_cmd_count =
    sizeof(s_cmd_table) / sizeof(s_cmd_table[0]);


/**
 * @brief  在接收到的帧中查找 $TEST...# 并执行（$TEST 大小写不敏感）
 */
static void process_frame(uint8_t *buf, uint16_t len)
{
    char *p_start, *p_end, *p_comma;
    char cmd[32], sub[32];
    uint8_t i;

    if (len == 0) return;

    buf[len] = '\0';

    /* 查找 $ 符号，然后检查后面是否跟的是 test（大小写不敏感） */
    p_start = (char *)buf;
    while (1)
    {
        p_start = strchr(p_start, '$');
        if (p_start == NULL) return;
        if (p_start - (char *)buf + 5 > len) return;

        char h0 = p_start[1], h1 = p_start[2], h2 = p_start[3], h3 = p_start[4];
        if (h0 >= 'A' && h0 <= 'Z') h0 += 'a' - 'A';
        if (h1 >= 'A' && h1 <= 'Z') h1 += 'a' - 'A';
        if (h2 >= 'A' && h2 <= 'Z') h2 += 'a' - 'A';
        if (h3 >= 'A' && h3 <= 'Z') h3 += 'a' - 'A';
        if (h0 == 't' && h1 == 'e' && h2 == 's' && h3 == 't')
            break;
        p_start++;
    }

    /* 查找 # 包尾 */
    p_end = strchr(p_start, '#');
    if (p_end == NULL) return;

    /* 跳过 "$TEST" */
    p_start += 5;
    if (*p_start == ',') p_start++;

    /* 分割 CMD 和 SUB */
    p_comma = strchr(p_start, ',');
    if (p_comma != NULL && p_comma < p_end)
    {
        *p_comma = '\0';
        *p_end = '\0';
        strncpy(cmd, p_start, sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
        strncpy(sub, p_comma + 1, sizeof(sub) - 1);
        sub[sizeof(sub) - 1] = '\0';

        /* 子命令后可能还有额外参数 */
        {
            char *p_param = strchr(sub, ',');
            if (p_param != NULL)
            {
                *p_param = '\0';
                p_param++;
                strncpy(g_test_param, p_param, sizeof(g_test_param) - 1);
                g_test_param[sizeof(g_test_param) - 1] = '\0';
            }
            else
            {
                g_test_param[0] = '\0';
            }
        }
    }
    else
    {
        *p_end = '\0';
        strncpy(cmd, p_start, sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
        sub[0] = '\0';
    }

    /* 保存子命令到全局（供 handler 使用） */
    strncpy(g_test_sub, sub, sizeof(g_test_sub) - 1);
    g_test_sub[sizeof(g_test_sub) - 1] = '\0';

    bsp_usart_printf(&s_usart1, "[EXT_TEST] CMD=%s, SUB=%s\r\n", cmd, sub);

    /* 查找命令表并执行 */
    for (i = 0; i < s_cmd_count; i++)
    {
        if (str_icmp(cmd, s_cmd_table[i].cmd) == 0)
        {
            if (s_cmd_table[i].sub[0] == '\0' ||
                str_icmp(sub, s_cmd_table[i].sub) == 0)
            {
                bsp_usart_printf(&s_usart1, "[EXT_TEST] Executing %s,%s...\r\n",
                                 s_cmd_table[i].cmd, s_cmd_table[i].sub);
                s_cmd_table[i].handler();
                return;
            }
        }
    }
    bsp_usart_printf(&s_usart1, "[EXT_TEST] Unknown command: %s,%s\r\n", cmd, sub);
}


/**
 * @brief  周期处理：检查 USART1 帧接收，解析并执行测试命令
 * @note   在定时器回调中由 scheduler 周期性调用
 */
void ext_hw_test_proc(void)
{
    uint8_t buf[128];
    uint16_t len;

    /* 检查串口命令接收 */
    if (bsp_usart_is_rx_complete(&s_usart1))
    {
        len = bsp_usart_recv_frame(&s_usart1, buf, sizeof(buf) - 1);
        if (len > 0)
            process_frame(buf, len);
    }
}
