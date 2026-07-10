/**
 * @file        SD.c
 * @brief       SD 卡外部硬件测试指令实现
 *              $Test,SD,MOUNT#                     — 初始化 SD + 挂载 FATFS + 打印卡信息
 *              $Test,SD,WRITE,路径,内容#            — 写入内容到指定文件
 *              $Test,SD,READ,路径#                  — 读取指定文件并串口输出
 *              $Test,SD,MKDIR,目录名#               — 创建目录
 *              $Test,SD,MKFILE,文件名#              — 创建空文件
 *              $Test,SD,LIST#                      — 列出根目录
 *              $Test,SD,DELETE,文件名#              — 删除文件/空目录
 *              $Test,SD,CSV#                       — 10万点 CSV 写入测试（SDRAM + 乒乓缓存）
 *              示例：
 *                $Test,SD,MOUNT#                          — 挂载
 *                $Test,SD,MKDIR,myfolder#                  — 创建子目录
 *                $Test,SD,WRITE,test.txt,HelloWorld#       — 写入根目录文件
 *                $Test,SD,WRITE,myfolder/data.txt,内容#    — 写入子目录文件
 *                $Test,SD,READ,test.txt#                   — 读取根目录文件
 *                $Test,SD,READ,myfolder/data.txt#          — 读取子目录文件
 *                $Test,SD,LIST#                            — 列出根目录
 *                $Test,SD,DELETE,myfolder/data.txt#        — 删除子目录下文件
 *                $Test,SD,DELETE,myfolder#                 — 删除空目录
 *                $Test,SD,CSV#                             — 启动 CSV 写入测试
 */

#include "SD.h"
#include "bsp_usart.h"
#include "ExtHardwareTest.h"
#include "ff.h"
#include "bsp_sdio_sd.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bsp_sram.h"

/* 串口引用（由 Main.c 定义）*/
extern struct bsp_usart s_usart1;

/* 测试参数（由 ExtHardwareTest.c 定义，通过 ExtHardwareTest.h 引用）*/

#include "scheduler.h"

/* FATFS 对象 */
static FATFS g_sd_fs;
static FIL   g_sd_file;

/* 挂载状态标志 */
static uint8_t g_sd_mounted = 0;


/* ============================================================================
 * SD LIST 异步状态机 — 每次 tick 只处理一个目录项，不阻塞调度器
 * ============================================================================ */
typedef enum {
    LIST_IDLE = 0,
    LIST_ROOT,        // 正在遍历根目录
    LIST_SUB,         // 正在遍历子目录
    LIST_DONE,        // 收尾打印统计
} sd_list_state_t;

static sd_list_state_t g_list_state = LIST_IDLE;
static DIR        g_list_dir;       // 根目录 DIR 对象
static DIR        g_list_subdir;    // 子目录 DIR 对象
static FILINFO    g_list_fno;       // 当前条目信息
static char       g_list_subpath[64]; // 子目录路径缓冲
static uint32_t   g_list_file_cnt;  // 文件计数
static uint32_t   g_list_dir_cnt;   // 目录计数

void sd_test_mount(void)
{
    FRESULT res;

    bsp_usart_printf(&s_usart1, "[SD] Initializing SD card...\r\n");

    /* 初始化 SD 卡硬件 */
    if (sd_init() != 0)
    {
        bsp_usart_printf(&s_usart1, "[SD] sd_init() failed!\r\n");
        return;
    }

    /* 挂载 FATFS（opt=1 立即挂载并初始化磁盘） */
    res = f_mount(&g_sd_fs, "0:", 1);
    if (res != FR_OK)
    {
        bsp_usart_printf(&s_usart1, "[SD] f_mount failed! res=%d\r\n", res);
        return;
    }
    g_sd_mounted = 1;

    /* 获取并打印卡信息 */
    {
        uint64_t capacity;
        uint32_t cap_mb;

        capacity = (uint64_t)g_sd_card_info.LogBlockNbr *
                   g_sd_card_info.LogBlockSize;
        cap_mb   = (uint32_t)(capacity >> 20);

        bsp_usart_printf(&s_usart1, "========== SD Card Info ==========\r\n");
        bsp_usart_printf(&s_usart1, "  CardType     = %lu\r\n",
                         g_sd_card_info.CardType);
        bsp_usart_printf(&s_usart1, "  CardVersion  = %lu\r\n",
                         g_sd_card_info.CardVersion);
        bsp_usart_printf(&s_usart1, "  Class        = %lu\r\n",
                         g_sd_card_info.Class);
        bsp_usart_printf(&s_usart1, "  RCA          = 0x%04lX\r\n",
                         g_sd_card_info.RelCardAdd);
        bsp_usart_printf(&s_usart1, "  BlockNbr     = %lu (%lu MB)\r\n",
                         g_sd_card_info.LogBlockNbr, cap_mb);
        bsp_usart_printf(&s_usart1, "  BlockSize    = %lu bytes\r\n",
                         g_sd_card_info.LogBlockSize);
        bsp_usart_printf(&s_usart1, "==================================\r\n");
    }

    bsp_usart_printf(&s_usart1, "[SD] Mount OK\r\n");
}

void sd_test_write(void)
{
    FRESULT res;
    UINT    bw;
    char    path[64];
    const char *content;
    size_t  content_len;
    char    *p_comma;

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1, "[SD] Not mounted, send $Test,SD,MOUNT# first\r\n");
        return;
    }

    if (g_test_param[0] == '\0')
    {
        bsp_usart_printf(&s_usart1, "[SD] Usage: $Test,SD,WRITE,路径,内容#\r\n");
        return;
    }

    /* 从 g_test_param 中解析路径和内容：第一个逗号前为路径，逗号后为内容 */
    p_comma = strchr(g_test_param, ',');
    if (p_comma == NULL)
    {
        bsp_usart_printf(&s_usart1, "[SD] Usage: $Test,SD,WRITE,路径,内容#  (missing content)\r\n");
        return;
    }
    *p_comma = '\0';

    /* 路径前面加 "0:/" */
    snprintf(path, sizeof(path), "0:/%s", g_test_param);

    content     = p_comma + 1;
    content_len = strlen(content);

    res = f_open(&g_sd_file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        bsp_usart_printf(&s_usart1, "[SD] Open %s failed! res=%d\r\n", path, res);
        *p_comma = ',';  /* 恢复 g_test_param */
        return;
    }

    res = f_write(&g_sd_file, content, (UINT)content_len, &bw);
    if (res != FR_OK || bw != (UINT)content_len)
    {
        bsp_usart_printf(&s_usart1, "[SD] Write failed! res=%d, bw=%u\r\n", res, bw);
        f_close(&g_sd_file);
        *p_comma = ',';
        return;
    }

    res = f_close(&g_sd_file);

    bsp_usart_printf(&s_usart1, "[SD] Write \"%s\" (%u bytes) to %s, f_close=%d\r\n",
                     content, (unsigned)content_len, path, res);

    *p_comma = ',';  /* 恢复 g_test_param */
}

void sd_test_read(void)
{
    FRESULT res;
    UINT    br;
    char    buf[513];
    char    path[64];

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1, "[SD] Not mounted, send $Test,SD,MOUNT# first\r\n");
        return;
    }

    if (g_test_param[0] == '\0')
    {
        bsp_usart_printf(&s_usart1, "[SD] Usage: $Test,SD,READ,路径#\r\n");
        return;
    }

    /* 使用 g_test_param 作为路径，前面加 "0:/" */
    snprintf(path, sizeof(path), "0:/%s", g_test_param);

    res = f_open(&g_sd_file, path, FA_READ);
    if (res != FR_OK)
    {
        bsp_usart_printf(&s_usart1, "[SD] Open %s failed! res=%d (not exist?)\r\n", path, res);
        return;
    }

    /* 一次性读入并显示 */
    memset(buf, 0, sizeof(buf));
    res = f_read(&g_sd_file, buf, sizeof(buf) - 1, &br);
    if (res != FR_OK)
    {
        bsp_usart_printf(&s_usart1, "[SD] Read failed! res=%d\r\n", res);
    }
    else
    {
        buf[br] = '\0';
        bsp_usart_printf(&s_usart1, "[SD] Read %u bytes from %s: <%s>\r\n", br, path, buf);
    }

    f_close(&g_sd_file);
}


void sd_test_mkdir(void)
{
    FRESULT res;
    char    path[64];

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1, "[SD] Not mounted, send $Test,SD,MOUNT# first\r\n");
        return;
    }

    if (g_test_param[0] == '\0')
    {
        bsp_usart_printf(&s_usart1, "[SD] Usage: $Test,SD,MKDIR,foldername#\r\n");
        return;
    }

    snprintf(path, sizeof(path), "0:/%s", g_test_param);
    res = f_mkdir(path);

    bsp_usart_printf(&s_usart1, "[SD] mkdir \"%s\": %s (res=%d)\r\n",
                     g_test_param, (res == FR_OK) ? "OK" : "FAIL", res);
}


void sd_test_mkfile(void)
{
    FRESULT res;
    char    path[64];

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1, "[SD] Not mounted, send $Test,SD,MOUNT# first\r\n");
        return;
    }

    if (g_test_param[0] == '\0')
    {
        bsp_usart_printf(&s_usart1, "[SD] Usage: $Test,SD,MKFILE,filename#\r\n");
        return;
    }

    snprintf(path, sizeof(path), "0:/%s", g_test_param);
    res = f_open(&g_sd_file, path, FA_CREATE_NEW | FA_WRITE);
    if (res == FR_OK)
    {
        f_close(&g_sd_file);
        bsp_usart_printf(&s_usart1, "[SD] mkfile \"%s\": OK\r\n", g_test_param);
    }
    else
    {
        bsp_usart_printf(&s_usart1, "[SD] mkfile \"%s\": FAIL (res=%d, maybe already exist)\r\n",
                         g_test_param, res);
    }
}


void sd_test_list(void)
{
    FRESULT res;

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1, "[SD] Not mounted, send $Test,SD,MOUNT# first\r\n");
        return;
    }

    if (g_list_state != LIST_IDLE)
    {
        bsp_usart_printf(&s_usart1, "[SD] List already in progress\r\n");
        return;
    }

    bsp_usart_send_str(&s_usart1,
        "========== SD directory tree ==========\r\n");

    res = f_opendir(&g_list_dir, "0:/");
    if (res != FR_OK)
    {
        bsp_usart_printf(&s_usart1, "[SD] opendir failed! res=%d\r\n", res);
        return;
    }

    g_list_file_cnt = 0;
    g_list_dir_cnt  = 0;
    g_list_state    = LIST_ROOT;
}


void sd_list_tick(void)
{
    FRESULT res;

    switch (g_list_state)
    {
    case LIST_ROOT:
        /* 读取根目录下一个条目 */
        res = f_readdir(&g_list_dir, &g_list_fno);
        if (res != FR_OK || g_list_fno.fname[0] == '\0')
        {
            /* 根目录遍历完毕 */
            f_closedir(&g_list_dir);
            g_list_state = LIST_DONE;
            return;
        }

        if (g_list_fno.fattrib & AM_DIR)
        {
            /* 跳过 Windows 系统目录 */
            if (strcmp(g_list_fno.fname, "System Volume Information") == 0)
                return;

            bsp_usart_printf(&s_usart1, "  [DIR ]  %s\r\n", g_list_fno.fname);
            g_list_dir_cnt++;

            /* 打开子目录，下次 tick 展开 */
            snprintf(g_list_subpath, sizeof(g_list_subpath),
                     "0:/%s", g_list_fno.fname);
            if (f_opendir(&g_list_subdir, g_list_subpath) == FR_OK)
                g_list_state = LIST_SUB;
        }
        else
        {
            bsp_usart_printf(&s_usart1, "  [FILE]  %-20s %lu bytes\r\n",
                             g_list_fno.fname, (unsigned long)g_list_fno.fsize);
            g_list_file_cnt++;
        }
        break;

    case LIST_SUB:
        /* 读取子目录下一个条目 */
        res = f_readdir(&g_list_subdir, &g_list_fno);
        if (res != FR_OK || g_list_fno.fname[0] == '\0')
        {
            /* 子目录遍历完毕，回到根目录 */
            f_closedir(&g_list_subdir);
            g_list_state = LIST_ROOT;
            return;
        }

        if (g_list_fno.fattrib & AM_DIR)
        {
            bsp_usart_printf(&s_usart1, "    [DIR ]  %s\r\n", g_list_fno.fname);
        }
        else
        {
            bsp_usart_printf(&s_usart1, "    [FILE]  %-20s %lu bytes\r\n",
                             g_list_fno.fname, (unsigned long)g_list_fno.fsize);
            g_list_file_cnt++;
        }
        break;

    case LIST_DONE:
        bsp_usart_printf(&s_usart1, "  %lu dir(s), %lu file(s)\r\n",
                         g_list_dir_cnt, g_list_file_cnt);
        bsp_usart_send_str(&s_usart1,
            "====================================\r\n");
        g_list_state = LIST_IDLE;
        break;

    default:
        break;
    }
}


void sd_test_delete(void)
{
    FRESULT res;
    char    path[64];

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1, "[SD] Not mounted, send $Test,SD,MOUNT# first\r\n");
        return;
    }

    if (g_test_param[0] == '\0')
    {
        bsp_usart_printf(&s_usart1, "[SD] Usage: $Test,SD,DELETE,filename#\r\n");
        return;
    }

    snprintf(path, sizeof(path), "0:/%s", g_test_param);
    res = f_unlink(path);

    if (res == FR_DENIED)
    {
        /* 尝试以目录方式打开，判断是文件还是目录 */
        DIR dir;
        if (f_opendir(&dir, path) == FR_OK)
        {
            f_closedir(&dir);
            bsp_usart_printf(&s_usart1, "[SD] delete \"%s\": directory not empty, delete files inside first (res=%d)\r\n",
                             g_test_param, res);
        }
        else
        {
            bsp_usart_printf(&s_usart1, "[SD] delete \"%s\": access denied, file locked or read-only (res=%d)\r\n",
                             g_test_param, res);
        }
        return;
    }

    bsp_usart_printf(&s_usart1, "[SD] delete \"%s\": %s (res=%d)\r\n",
                     g_test_param, (res == FR_OK) ? "OK" : "FAIL", res);
}


/* ============================================================================
 * 纯函数接口（带形参，不依赖 g_test_param/g_test_path 全局变量）
 *
 * 用法示例：
 *   #include "SD.h"
 *
 *   // === 基础操作 ===
 *   sd_mount();                              // ① 初始化 SD + 挂载 FATFS
 *   sd_write_file("0:/test.txt", "Hello");   // ② 写入根目录文件
 *   sd_read_file("0:/test.txt", buf, sizeof(buf), &br); // ③ 读取
 *   sd_list("0:/");                          // ④ 列出根目录
 *   sd_delete("0:/test.txt");                // ⑤ 删除文件
 *
 *   // === 子目录操作 ===
 *   sd_mkdir("0:/myfolder");                          // 创建子目录
 *   sd_write_file("0:/myfolder/data.txt", "内容");    // 写入子目录文件
 *   sd_read_file("0:/myfolder/data.txt", buf, sizeof(buf), &br);
 *   sd_delete("0:/myfolder/data.txt");                // 删除子目录文件
 *   sd_delete("0:/myfolder");                         // 删除空目录
 *
 *   // === 创建空文件 ===
 *   sd_mkfile("0:/new.txt");                // 文件不存在则创建，存在返回 FR_EXIST
 * ============================================================================ */


FRESULT sd_mount(void)
{
    if (sd_init() != 0)
        return FR_DISK_ERR;

    FRESULT res = f_mount(&g_sd_fs, "0:", 1);
    if (res == FR_OK)
        g_sd_mounted = 1;
    return res;
}


FRESULT sd_write_file(const char *path, const char *content)
{
    FIL     file;
    UINT    bw;
    FRESULT res;

    if (!g_sd_mounted) return FR_NOT_READY;
    if (path == NULL || content == NULL) return FR_INVALID_PARAMETER;

    res = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK) return res;

    res = f_write(&file, content, (UINT)strlen(content), &bw);
    if (res != FR_OK)
    {
        f_close(&file);
        return res;
    }

    /* f_close 会刷写缓存到 SD 卡，必须检查其返回值 */
    return f_close(&file);
}


FRESULT sd_read_file(const char *path, void *buf, UINT buf_sz, UINT *br)
{
    FIL     file;
    FRESULT res;

    if (!g_sd_mounted) return FR_NOT_READY;
    if (path == NULL || buf == NULL || br == NULL) return FR_INVALID_PARAMETER;

    res = f_open(&file, path, FA_READ);
    if (res != FR_OK) return res;

    *br = 0;
    res = f_read(&file, buf, buf_sz, br);
    f_close(&file);
    return res;
}


FRESULT sd_mkdir(const char *path)
{
    if (!g_sd_mounted) return FR_NOT_READY;
    if (path == NULL) return FR_INVALID_PARAMETER;
    return f_mkdir(path);
}


FRESULT sd_mkfile(const char *path)
{
    FIL     file;
    FRESULT res;

    if (!g_sd_mounted) return FR_NOT_READY;
    if (path == NULL) return FR_INVALID_PARAMETER;

    res = f_open(&file, path, FA_CREATE_NEW | FA_WRITE);
    if (res != FR_OK)
        return res;

    res = f_close(&file);
    if (res != FR_OK)
        return res;

    return FR_OK;
}


FRESULT sd_delete(const char *path)
{
    if (!g_sd_mounted) return FR_NOT_READY;
    if (path == NULL) return FR_INVALID_PARAMETER;
    return f_unlink(path);
}


FRESULT sd_list(const char *path)
{
    DIR     dir;
    FILINFO fno;
    FRESULT res;
    uint32_t file_cnt = 0, dir_cnt = 0;

    if (!g_sd_mounted) return FR_NOT_READY;
    if (path == NULL) path = "0:/";

    bsp_usart_send_str(&s_usart1,
        "========== SD directory tree ==========\r\n");

    res = f_opendir(&dir, path);
    if (res != FR_OK) return res;

    for (;;)
    {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == '\0')
            break;

        if (fno.fattrib & AM_DIR)
        {
            if (strcmp(fno.fname, "System Volume Information") == 0)
                continue;

            bsp_usart_printf(&s_usart1, "  [DIR ]  %s\r\n", fno.fname);
            dir_cnt++;

            /* 展开子目录下一层内容 */
            {
                DIR     subdir;
                FILINFO subfno;
                char    subpath[64];

                snprintf(subpath, sizeof(subpath), "%s%s%s",
                         path,
                         (path[strlen(path) - 1] == '/') ? "" : "/",
                         fno.fname);

                if (f_opendir(&subdir, subpath) == FR_OK)
                {
                    for (;;)
                    {
                        res = f_readdir(&subdir, &subfno);
                        if (res != FR_OK || subfno.fname[0] == '\0')
                            break;

                        if (subfno.fattrib & AM_DIR)
                        {
                            bsp_usart_printf(&s_usart1, "    [DIR ]  %s\r\n", subfno.fname);
                        }
                        else
                        {
                            bsp_usart_printf(&s_usart1, "    [FILE]  %-20s %lu bytes\r\n",
                                             subfno.fname, (unsigned long)subfno.fsize);
                            file_cnt++;
                        }
                    }
                    f_closedir(&subdir);
                }
            }
        }
        else
        {
            bsp_usart_printf(&s_usart1, "  [FILE]  %-20s %lu bytes\r\n",
                             fno.fname, (unsigned long)fno.fsize);
            file_cnt++;
        }
    }

    f_closedir(&dir);

    bsp_usart_printf(&s_usart1, "  %lu dir(s), %lu file(s)\r\n", dir_cnt, file_cnt);
    bsp_usart_send_str(&s_usart1,
        "====================================\r\n");
    return FR_OK;
}


/* ============================================================================
 * CSV 写入测试 — F407 外挂 SRAM 版（异步状态机，GEN+WRITE 合并执行）
 *
 * 设计概要：
 *   CPU 生成 CSV 行 → 外挂 SRAM 暂存 (0x68000000, 512KB 工作区)
 *   → 同一 tick 内立即 f_write 写入 SD 卡
 *   → 重复直到 4 万点写完。
 *
 *   F4 DMA2 可直接访问 FSMC 总线 (SRAM 在 FSMC Bank3)，无需中间缓存。
 *   每 tick 最多处理 CSV_TICK_BYTES 字节（当前 256KB），
 *   确保单次 f_write 耗时 < 调度器周期，不阻塞其他任务。
 *
 * 关键参数关系：
 *   CSV_TICK_BYTES * (1.7MB / CSV_TICK_BYTES + 3) ≈ 总耗时
 *   其中 1.7MB = 40000 行 CSV 近似总大小。
 *   CSV_TICK_BYTES 越大 → 写入次数越少 → 总耗时越短
 *   CSV_TICK_BYTES 越大 → 单 tick 耗时越长 → 需要更大调度器周期
 *
 * 串口指令: $Test,SD,CSV#
 * ============================================================================ */

#define CSV_POINTS          100000       /* 10 万个数据点 */
#define CSV_BUF_SIZE        524288      /* SRAM 工作区 (512KB) */
#define CSV_TICK_BYTES      262144      /* 每 tick 生成+写入的数据量 (256KB) */

/* 状态机 */
typedef enum {
    CSV_IDLE = 0,
    CSV_OPEN,           /* 刚打开文件，下一个 tick 才开始生成 + 写入 */
    CSV_TICK,           /* 每 tick: 生成一批 + 立即写入 */
    CSV_DONE,           /* 打印统计 */
    CSV_CLOSE,          /* 关闭文件 (可能耗时数十ms, 单独一个 tick) */
} csv_state_t;

/* ---- 持久变量 ---- */
static csv_state_t  g_csv_state = CSV_IDLE;
static uint8_t      g_csv_busy = 0;              /* 重入保护 */


static uint32_t g_csv_gen_idx;                   /* 当前已生成的点序号 (0~CSV_POINTS-1) */
static uint32_t g_csv_write_cnt;                 /* 写入次数 */
static uint32_t g_csv_start_tick;                /* 计时基准 */
static FIL      g_csv_file;


/* ---- 辅助函数：无符号整数 转 字符串，返回写入指针 ---- */
/*
 * 直接正向写入，不经过临时数组反转。
 * 按数量级依次剥离高位，写出所有有效位。
 */
static char *csv_put_u32(char *p, uint32_t val)
{
    if (val == 0)
    {
        *p++ = '0';
        return p;
    }

    /* 找到 ≤ val 的最大 10 的幂 */
    uint32_t pow10 = 1;
    uint32_t tmp = val;
    while (tmp >= 10)
    {
        tmp /= 10;
        pow10 *= 10;
    }

    /* 从高位到低位逐个输出 */
    while (pow10 > 0)
    {
        uint32_t digit = val / pow10;
        *p++ = '0' + (char)digit;
        val -= digit * pow10;
        pow10 /= 10;
    }
    return p;
}


/* 固定 3 位小数，不足补零 */
static char *csv_put_3dig(char *p, uint32_t val)
{
    *p++ = '0' + (char)(val / 100);
    *p++ = '0' + (char)((val / 10) % 10);
    *p++ = '0' + (char)(val % 10);
    return p;
}


/*
 * 生成一行 CSV 数据
 *
 *   Index,BLANK,Position,Force,Analog,Time,Speed,Voltage,Step
 *   0,,0.000,0.000,0,0,0,0.00,0
 *   1,,0.001,0.001,1,1,1,-0.00,0
 *   ...
 *
 * @param  p  输出指针
 * @param  i  数据点序号 (0 ~ CSV_POINTS-1)
 * @return    写入结束位置
 */
static char *csv_gen_line(char *p, uint32_t i)
{
    uint32_t q = i / 1000;      /* 整数部分 */
    uint32_t r = i % 1000;      /* 小数部分 */

    /* Index */
    p = csv_put_u32(p, i);           *p++ = ',';

    /* BLANK（空字段） */
    *p++ = ',';

    /* Position (xxx.xxx) */
    p = csv_put_u32(p, q);           *p++ = '.';
    p = csv_put_3dig(p, r);          *p++ = ',';

    /* Force (xxx.xxx) */
    p = csv_put_u32(p, q);           *p++ = '.';
    p = csv_put_3dig(p, r);          *p++ = ',';

    /* Analog */
    p = csv_put_u32(p, i);           *p++ = ',';

    /* Time */
    p = csv_put_u32(p, i);           *p++ = ',';

    /* Speed */
    p = csv_put_u32(p, i);           *p++ = ',';

    /* Voltage (-xxx.xx，奇数 q 为负) */
    if ((q & 1U) && q > 0)
        *p++ = '-';
    p = csv_put_u32(p, q);           *p++ = '.';
    *p++ = '0' + (char)((r / 10) % 10);
    *p++ = '0' + (char)(r % 10);

    /* Step (始终为 0) */
    *p++ = ',';
    *p++ = '0';

    *p++ = '\n';

    return p;
}



/**
 * @brief  CSV 写入测试入口 — 打开文件 + 写文件头
 *
 * 在串口命令处理器中调用。只做初始化并设置 g_csv_state，
 * 后续数据生成和写入由 sd_csv_tick() 异步驱动。
 *
 * 串口指令: $Test,SD,CSV#
 */
void sd_test_csv(void)
{
    if (g_csv_state != CSV_IDLE)
    {
        bsp_usart_printf(&s_usart1,
            "[CSV] 上一个测试仍在进行中，请等待完成\r\n");
        return;
    }

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1,
            "[CSV] SD not mounted! Please send $Test,SD,MOUNT#\r\n");
        return;
    }

    /* 打开文件（覆盖创建） */
    FRESULT res = f_open(&g_csv_file, "0:/csv_test666.csv",
                         FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        bsp_usart_printf(&s_usart1,
            "[CSV] 打开文件失败! res=%d\r\n", res);
        return;
    }

    /* 写 CSV 文件头 + 追加首批数据，一次性扇区对齐写入。
     * 避免 header 后出现大量空行（单纯 padding 会导致此问题）。
     * 扇区对齐确保后续 CSV_TICK 全部走 Direct Write 路径，
     * 消除 FATFS 部分扇区缓存写入脏数据的 bug。 */
    {
        char *p = (char *)EXT_SRAM_ADDR;
        p += sprintf(p, "Version,V1.0.1\n");
        p += sprintf(p, "ProgramID,\n");
        p += sprintf(p, "ProgramName,\n");
        p += sprintf(p, "DateTime,\n");
        p += sprintf(p, "XAxisUnit,\n");
        p += sprintf(p, "ForceUnit,\n");
        p += sprintf(p, "Result,\n");
        p += sprintf(p, "Tolerance Windows,\n");
        p += sprintf(p, "ProductSN,\n");	
        p += sprintf(p, "ComponentSN,\n");
        p += sprintf(p, "ModelNumber,\n");
        p += sprintf(p, "Index,BLANK,Position,Force,Analog,Time,Speed,Voltage,Step\n");

        /* 追加首批 CSV 数据行，使总长度尽量靠近 512 字节边界
         * （512 余量 > 480 → 到下一个边界的浪费 ≤ 31 字节）。
         * 最多生成 3 个扇区（约 100 行），避免大量 '\n' 填充。 */
        g_csv_gen_idx = 0;
        while (g_csv_gen_idx < CSV_POINTS)
        {
            uint32_t off = (uint32_t)(p - (char *)EXT_SRAM_ADDR);
            if (off >= 1024U)
            {
                uint32_t rem = off & 511U;            /* off % 512 */
                if (rem > 480U) break;                 /* 离下一边界 ≤ 31 字节 */
                if (off > 4096U) break;                 /* 最多生成 ~8 扇区 */
            }
            p = csv_gen_line(p, g_csv_gen_idx);
            g_csv_gen_idx++;
        }

        /* 对齐到 512 字节边界 */
        uint32_t chunk = (uint32_t)(p - (char *)EXT_SRAM_ADDR);
        uint32_t aligned = (chunk + 511U) & ~511U;
        for (uint32_t i = chunk; i < aligned; i++)
            *p++ = '\n';

        UINT bw;
        res = f_write(&g_csv_file, (const void *)EXT_SRAM_ADDR,
                      aligned, &bw);
        if (res != FR_OK || bw != aligned)
        {
            bsp_usart_printf(&s_usart1,
                "[CSV] 写文件头失败! res=%d\r\n", res);
            f_close(&g_csv_file);
            return;
        }
        f_sync(&g_csv_file);
    }

    /* 重置状态机（g_csv_gen_idx 已在上面推进） */
    g_csv_write_cnt = 1;      /* 已写 header + 首批数据算一次 */
    g_csv_start_tick = HAL_GetTick();

    bsp_usart_printf(&s_usart1,
        "[CSV] start %lu points CSV write (buffer %u bytes)...\r\n",
        (unsigned long)CSV_POINTS, (unsigned int)CSV_BUF_SIZE);

    g_csv_state = CSV_OPEN;
}


/**
 * @brief  CSV 异步状态机 tick 驱动
 *
 * 由 ext_hw_test_proc 调度器任务按配置周期调用。
 *
 * 每个 CSV_TICK 在一个调用内完成两个步骤：
 *   1. GEN   — 生成一批 CSV 数据到外挂 SRAM (最多 CSV_TICK_BYTES)
 *   2. WRITE — 立即 f_write 写入 SD 卡
 *
 * 关键：GEN + WRITE 在同一 tick 内完成，避免分多个 tick 写入
 * 导致总耗时被调度器间隔大幅放大。
 *
 * 状态机时序：
 *   CSV_OPEN  (空跑一 tick，让 f_open 与 CSV_TICK 分开)
 *   → CSV_TICK × N (GEN+WRITE 直到全部点生成完毕)
 *   → CSV_DONE (打印统计)
 *   → CSV_CLOSE (关闭文件，f_close 可能耗时 20-30ms)
 *   → CSV_IDLE
 *
 * buffer 与周期关系（实测 @ F407 168MHz, SD 24MHz 4-bit）：
 *   CSV_TICK_BYTES   单 tick 耗时   推荐调度周期     写入次数    总耗时
 *   64KB             ~19ms          30ms             28 次      ~826ms
 *   128KB            ~38ms          50ms             15 次      ~740ms
 *   256KB  ←当前     ~77ms          90ms             7 次       ~703ms
 *   512KB            ~155ms         200ms            4 次       ~984ms
 *   结论：256KB@90ms 是当前最优平衡点，速度和实时性兼顾。
 */
void sd_csv_tick(void)
{
    if (g_csv_state == CSV_IDLE) return;
    if (g_csv_busy) return;              /* 重入保护 */
    g_csv_busy = 1;

    switch (g_csv_state)
    {
    /* ------------------------------------------------------------------ */
    case CSV_OPEN:
    {
        /* 此 tick 空跑，让 sd_test_csv() 中的 f_open + f_write header
         * 与第一个 CSV_TICK 的生成+写入不在同一个 tick，
         * 避免串口命令处理 + 文件打开耗时代码挤在一起。
         */
        /* 记下真正开始生成数据的时间 */
        g_csv_start_tick = HAL_GetTick();
        g_csv_state = CSV_TICK;
        break;
    }

    /* ------------------------------------------------------------------ */
    case CSV_TICK:
    {
        /* ---- 1. 生成一批数据到 SRAM ---- */
        char *p = (char *)EXT_SRAM_ADDR;
        char *end = p + CSV_TICK_BYTES;

        while (g_csv_gen_idx < CSV_POINTS &&
               (uint32_t)(end - p) > 60U)   /* 留一行余量 */
        {
            p = csv_gen_line(p, g_csv_gen_idx);
            g_csv_gen_idx++;
        }
        uint32_t len = (uint32_t)(p - (char *)EXT_SRAM_ADDR);

        /* ---- 2. 补 '\n' 对齐到 512 字节边界，避免 FATFS
         *        部分扇区缓存写入脏数据 ----
         * CSV_BUF_SIZE(512KB) 足够容纳 CSVTICK_BYTES(256KB)+511 对齐 */
        uint32_t len_aligned = (len + 511U) & ~511U;
        for (uint32_t i = len; i < len_aligned; i++)
            ((char *)EXT_SRAM_ADDR)[i] = '\n';

        /* ---- 3. 立即写入 SD 卡 ---- */
        if (len_aligned > 0)
        {
            UINT bw;
            FRESULT res = f_write(&g_csv_file,
                        (const void *)EXT_SRAM_ADDR, len_aligned, &bw);
            if (res != FR_OK || bw != len_aligned)
            {
                bsp_usart_printf(&s_usart1,
                    "[CSV] 写入失败! idx=%lu res=%d bw=%u/%lu\r\n",
                    (unsigned long)g_csv_gen_idx, res,
                    (unsigned)bw, (unsigned long)len_aligned);
                f_close(&g_csv_file);
                g_csv_state = CSV_IDLE;
                break;
            }
            g_csv_write_cnt++;
        }

        /* 全部点生成完毕？ */
        if (g_csv_gen_idx >= CSV_POINTS)
            g_csv_state = CSV_DONE;
        break;
    }

    /* ------------------------------------------------------------------ */
    case CSV_DONE:
    {
        /* 关闭文件前获取文件大小 */
        uint32_t file_size = (uint32_t)f_size(&g_csv_file);
        uint32_t elapsed = HAL_GetTick() - g_csv_start_tick;
        if (elapsed == 0) elapsed = 1;
        uint32_t speed_kBps =
            (uint32_t)((uint64_t)file_size * 1000UL / 1024UL / elapsed);

        bsp_usart_printf(&s_usart1,
            "========== CSV write summary ==========\r\n"
            "  filename   csv_test.csv\r\n"
            "  points     %lu\r\n"
            "  buffer     %u bytes (per tick %u bytes)\r\n"
            "  file size  %lu bytes (%lu KB)\r\n"
            "  writes     %lu\r\n"
            "  elapsed    %lu ms\r\n"
            "  speed      %lu KB/s\r\n"
            "=======================================\r\n",
            (unsigned long)CSV_POINTS, (unsigned int)CSV_BUF_SIZE,
            (unsigned int)CSV_TICK_BYTES,
            (unsigned long)file_size, (unsigned long)(file_size / 1024),
            (unsigned long)g_csv_write_cnt,
            (unsigned long)elapsed,
            (unsigned long)speed_kBps);

        /* 不在此处 f_close，切到 CSV_CLOSE 单独一个 tick 关闭 */
        g_csv_state = CSV_CLOSE;
        break;
    }

    /* ------------------------------------------------------------------ */
    case CSV_CLOSE:
    {
        f_close(&g_csv_file);
        g_csv_state = CSV_IDLE;
        break;
    }

    default:
        g_csv_state = CSV_IDLE;
        break;
    }

    g_csv_busy = 0;
}


/* ============================================================================
 * sd_pure_test — FATFS 纯函数功能全面测试
 *
 * 串口指令: $Test,SD,PURE#
 * 代码调用: sd_pure_test();
 *
 * 测试内容：写文件→读回校验→子目录→删除→LIST
 * ============================================================================ */
void sd_pure_test(void)
{
    char buf[512];
    UINT  br;
    int   res;
    int   pass = 1;

    /* [1] sd_write_file - write to root (全新文件名避免残留) */
    bsp_usart_printf(&s_usart1, "[1] sd_write_file(\"0:/pure_write_test.txt\", \"HelloWorld\") = ");
    res = sd_write_file("0:/pure_write_test.txt", "HelloWorld");
    bsp_usart_printf(&s_usart1, "%d", res);
    if (res != FR_OK) { bsp_usart_send_str(&s_usart1, " FAIL"); pass = 0; }
    bsp_usart_send_str(&s_usart1, "\r\n");

    /* [2] sd_read_file - read back and verify */
    bsp_usart_printf(&s_usart1, "[2] sd_read_file(\"0:/pure_write_test.txt\") = ");
    memset(buf, 0, sizeof(buf));
    res = sd_read_file("0:/pure_write_test.txt", buf, sizeof(buf) - 1, &br);
    bsp_usart_printf(&s_usart1, "res=%d, %u bytes: <%s>", res, br, buf);
    if (res == FR_OK && br == 10 && memcmp(buf, "HelloWorld", 10) == 0)
        bsp_usart_send_str(&s_usart1, " MATCH");
    else
        { bsp_usart_send_str(&s_usart1, " MISMATCH"); pass = 0; }
    bsp_usart_send_str(&s_usart1, "\r\n");

    /* [3] sd_write_file - write another file to root */
    bsp_usart_printf(&s_usart1, "[3] sd_write_file(\"0:/pure_test2.txt\", \"Test2Data\") = ");
    res = sd_write_file("0:/pure_test2.txt", "Test2Data");
    bsp_usart_printf(&s_usart1, "%d", res);
    if (res != FR_OK) { bsp_usart_send_str(&s_usart1, " FAIL"); pass = 0; }
    bsp_usart_send_str(&s_usart1, "\r\n");

    /* [4] sd_read_file - read second file */
    bsp_usart_printf(&s_usart1, "[4] sd_read_file(\"0:/pure_test2.txt\") = ");
    memset(buf, 0, sizeof(buf));
    res = sd_read_file("0:/pure_test2.txt", buf, sizeof(buf) - 1, &br);
    bsp_usart_printf(&s_usart1, "res=%d, %u bytes: <%s>", res, br, buf);
    if (res == FR_OK && br == 9 && memcmp(buf, "Test2Data", 9) == 0)
        bsp_usart_send_str(&s_usart1, " MATCH");
    else
        { bsp_usart_send_str(&s_usart1, " MISMATCH"); pass = 0; }
    bsp_usart_send_str(&s_usart1, "\r\n");

    /* [5] sd_mkdir + sd_write_file - write to subdir */
    bsp_usart_printf(&s_usart1, "[5] sd_mkdir(\"0:/pure_sub\") = ");
    res = sd_mkdir("0:/pure_sub");
    bsp_usart_printf(&s_usart1, "%d", res);
    if (res != FR_OK && res != FR_EXIST) { pass = 0; }
    bsp_usart_printf(&s_usart1, "  sd_write_file(\"0:/pure_sub/data.txt\", \"1234567\") = ");
    res = sd_write_file("0:/pure_sub/data.txt", "1234567");
    bsp_usart_printf(&s_usart1, "%d", res);
    if (res != FR_OK) { bsp_usart_send_str(&s_usart1, " FAIL"); pass = 0; }
    bsp_usart_send_str(&s_usart1, "\r\n");

    /* [6] sd_read_file - read subdir file */
    bsp_usart_printf(&s_usart1, "[6] sd_read_file(\"0:/pure_sub/data.txt\") = ");
    memset(buf, 0, sizeof(buf));
    res = sd_read_file("0:/pure_sub/data.txt", buf, sizeof(buf) - 1, &br);
    bsp_usart_printf(&s_usart1, "res=%d, %u bytes: <%s>", res, br, buf);
    if (res == FR_OK && br == 7 && memcmp(buf, "1234567", 7) == 0)
        bsp_usart_send_str(&s_usart1, " MATCH");
    else
        { bsp_usart_send_str(&s_usart1, " MISMATCH"); pass = 0; }
    bsp_usart_send_str(&s_usart1, "\r\n");

    /* [7] sd_mkdir - create another dir */
    bsp_usart_printf(&s_usart1, "[7] sd_mkdir(\"0:/pure_other\") = ");
    res = sd_mkdir("0:/pure_other");
    bsp_usart_printf(&s_usart1, "%d\r\n", res);

    /* [8] sd_mkfile - create empty file */
    bsp_usart_printf(&s_usart1, "[8] sd_mkfile(\"0:/pure_empty.txt\") = ");
    res = sd_mkfile("0:/pure_empty.txt");
    bsp_usart_printf(&s_usart1, "%d\r\n", res);

    /* [9] sd_mkfile - path not exist */
    bsp_usart_printf(&s_usart1, "[9] sd_mkfile(\"0:/no_dir/x.txt\") = ");
    res = sd_mkfile("0:/no_dir/x.txt");
    bsp_usart_printf(&s_usart1, "%d (expect 5)\r\n", res);

    /* [10] sd_list */
    bsp_usart_send_str(&s_usart1, "[10] sd_list(\"0:/\"):\r\n");
    sd_list("0:/");

    /* [11] sd_delete subdir file */
    bsp_usart_printf(&s_usart1, "[11] sd_delete(\"0:/pure_sub/data.txt\") = ");
    res = sd_delete("0:/pure_sub/data.txt");
    bsp_usart_printf(&s_usart1, "%d\r\n", res);

    /* [12] sd_delete empty dir */
    bsp_usart_printf(&s_usart1, "[12] sd_delete(\"0:/pure_sub\") = ");
    res = sd_delete("0:/pure_sub");
    bsp_usart_printf(&s_usart1, "%d\r\n", res);

    /* [13] sd_delete non-empty dir (should fail) */
    res = sd_mkfile("0:/pure_other/keep.txt");
    bsp_usart_printf(&s_usart1, "[13] sd_mkfile(\"0:/pure_other/keep.txt\") = %d", res);
    bsp_usart_printf(&s_usart1, "  then sd_delete(\"0:/pure_other\") = ");
    res = sd_delete("0:/pure_other");
    bsp_usart_printf(&s_usart1, "%d (expect 7)\r\n", res);

    /* [14] final verify */
    bsp_usart_send_str(&s_usart1, "[14] final sd_list(\"0:/\"):\r\n");
    sd_list("0:/");

    bsp_usart_printf(&s_usart1, "========== SD pure function test %s ==========\r\n",
                     pass ? "PASSED" : "FAILED");
}


/* ============================================================================
 * sd_stress_test — SDIO 低层级压力测试
 *
 * 串口指令: $Test,SD,STRESS#
 * 代码调用: sd_stress_test(100000, 1000);
 *
 * 向安全扇区反复写→读→校验，测试 24MHz 4-bit DMA 稳定性。
 * ============================================================================ */
void sd_stress_test(uint32_t sector, int rounds)
{
    uint8_t wbuf[512], rbuf[512];
    int i, r;
    int fail = 0;
    uint32_t start = HAL_GetTick();

    bsp_usart_printf(&s_usart1, "\r\n===== SD STRESS TEST (%d rounds @ sector %lu) =====\r\n",
                     rounds, (unsigned long)sector);

    for (r = 0; r < rounds; r++)
    {
        for (i = 0; i < 512 / 4; i++)
            ((uint32_t *)wbuf)[i] = 0xDEAD0000UL + (uint32_t)(r * 512 / 4 + i);

        if (sd_write_disk(wbuf, sector + (r & 0x0F), 1) != 0)
        {
            bsp_usart_printf(&s_usart1, "[STRESS] WRITE FAIL at round %d\r\n", r);
            fail = 1; break;
        }

        memset(rbuf, 0, sizeof(rbuf));
        if (sd_read_disk(rbuf, sector + (r & 0x0F), 1) != 0)
        {
            bsp_usart_printf(&s_usart1, "[STRESS] READ FAIL at round %d\r\n", r);
            fail = 1; break;
        }

        if (memcmp(wbuf, rbuf, 512) != 0)
        {
            bsp_usart_printf(&s_usart1, "[STRESS] DATA MISMATCH at round %d\r\n", r);
            fail = 1; break;
        }

        if ((r % 100) == 99)
            bsp_usart_printf(&s_usart1, "[STRESS] %d/%d rounds passed\r\n", r + 1, rounds);
    }

    if (!fail)
        bsp_usart_printf(&s_usart1, "[STRESS] ALL %d rounds PASSED in %lu ms\r\n",
                         rounds, (unsigned long)(HAL_GetTick() - start));
    else
        bsp_usart_printf(&s_usart1, "[STRESS] FAILED at round %d\r\n", r);
}


/* ============================================================================
 * sd_test_pure — 串口指令 $Test,SD,PURE#
 * ============================================================================ */
void sd_test_pure(void)
{
    sd_pure_test();
}


/* ============================================================================
 * sd_test_stress — 串口指令 $Test,SD,STRESS#
 *
 * 可选参数：$Test,SD,STRESS,轮数#
 *   如 $Test,SD,STRESS,5000# 则跑 5000 轮
 *   不指定轮数默认 10000 轮
 * ============================================================================ */
void sd_test_stress(void)
{
    int rounds = 10000;

    if (g_test_param[0] != '\0')
    {
        int n = 0;
        const char *p = g_test_param;
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (int)(*p++ - '0');
        if (n > 0)
            rounds = n;
    }

    sd_stress_test(100000, rounds);
}
