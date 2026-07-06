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

/* 串口引用（由 Main.c 定义）*/
extern struct bsp_usart s_usart1;

/* 测试参数（由 ExtHardwareTest.c 定义，通过 ExtHardwareTest.h 引用）*/

#include "scheduler.h"

/* FATFS 对象 */
static FATFS g_sd_fs;
static FIL   g_sd_file;

/* 挂载状态标志 */
static uint8_t g_sd_mounted = 0;

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
    DIR     dir;
    FILINFO fno;
    uint32_t file_cnt = 0, dir_cnt = 0;

    if (!g_sd_mounted)
    {
        bsp_usart_printf(&s_usart1, "[SD] Not mounted, send $Test,SD,MOUNT# first\r\n");
        return;
    }

    bsp_usart_send_str(&s_usart1,
        "========== SD directory tree ==========\r\n");

    res = f_opendir(&dir, "0:/");
    if (res != FR_OK)
    {
        bsp_usart_printf(&s_usart1, "[SD] opendir failed! res=%d\r\n", res);
        return;
    }

    for (;;)
    {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == '\0')
            break;

        if (fno.fattrib & AM_DIR)
        {
            /* 跳过 Windows 系统目录 */
            if (strcmp(fno.fname, "System Volume Information") == 0)
                continue;

            bsp_usart_printf(&s_usart1, "  [DIR ]  %s\r\n", fno.fname);
            dir_cnt++;

            /* 展开子目录下一层内容 */
            {
                DIR     subdir;
                FILINFO subfno;
                char    subpath[64];

                snprintf(subpath, sizeof(subpath), "0:/%s", fno.fname);
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
 * CSV 写入测试 — F407 暂不使用
 * ============================================================================ */
#if 0

/* ============================================================================
 * CSV 写入测试（100,000 个数据点）— 部分异步状态机版本
 *
 * 数据管道：
 *   Phase 1:  AXISRAM buf_A/B 通过 MDMA(中断) 到 SDRAM(累计)
 *   Phase 2:  SDRAM 通过 memcpy 到 AXISRAM buf_A/B 通过 IDMA 到 SD 卡
 *
 * Phase 1 与 Phase 2 共用同一组 2 块 AXISRAM 乒乓缓冲区，节省内存。
 * Phase 2 不能直接从 SDRAM 到 SD 卡，因为 SDMMC IDMA 只能访问
 * AXISRAM（D1 域），SDRAM 在 D2 域（FMC 总线），跨域 IDMA 无法读取。
 *
 * 异步化改造：
 *   整个写入流程由 sd_csv_tick() 在每个 ext_hw_test_proc 的 10ms 定时
 *   tick 中执行一个状态步骤后立即返回，不阻塞 while(1) 主循环。
 *
 *   Phase 1: CPU 填满 64KB buf 后启动 MDMA 中断搬运，待下次 tick 再处理
 *            下一块。MDMA 传输完成回调仅设标志，不消耗 Tick 时间。
 *
 *   Phase 2: f_write 阻塞约 1-2ms（SD 卡内部 NAND 编程），可接受。
 *
 * 串口指令: $Test,SD,CSV#
 * 代码调用: sd_test_csv()  +  sd_csv_tick() 驱动
 * ============================================================================ */

#define CSV_TEST_POINTS     100000      /* 10 万个数据点 */
#define CSV_CHUNK_SIZE      65536       /* 单块大小 (64KB)，两个乒乓缓冲区各 64KB，共 128KB */
#define CSV_SDRAM_BUF_SIZE  (8 * 1024 * 1024)  /* SDRAM 中构建数据的最大容量 */


/* ============================================================================
 * CSV 异步状态机 — 完全非阻塞设计
 *
 * 设计要点：
 *   1. 状态机每 10ms (ext_hw_test_proc tick) 执行一个步骤后立即返回
 *   2. Phase 1: MDMA 中断驱动，CPU 填 buf 后立即返回，MDMA 完成后中断通知
 *   3. Phase 2: f_write 阻塞 1-2ms（SD 卡内部 NAND 编程），忙等中通过
 *      sd_dma_busy_poll_hook 让出 CPU，不阻塞 while(1) 主循环
 *   4. 缓冲区从函数局部变量提升为文件作用域 static，以便在各 tick 间保持
 * ============================================================================ */
typedef enum {
    CSV_IDLE = 0,
    CSV_P1_FILL,       /* Phase 1: 填 buf[sel] → 启 MDMA */
    CSV_P2_PREP,       /* Phase 2: 初始化(打开文件 + 计时) */
    CSV_P2_WRITE,      /* Phase 2: memcpy SDRAM→buf + f_write 单块 */
    CSV_DONE,          /* 收尾: 关闭文件 + 打印统计 */
} csv_state_t;

/* ---- 状态机持久变量 ---- */
static csv_state_t      g_csv_state = CSV_IDLE;
static volatile uint8_t g_csv_mdma_flag = 1;    /* MDMA 传输完成 (中断置位) */
static uint8_t          g_csv_busy = 0;           /* 重入保护 */

/* Phase 1 持久变量 */
static uint32_t csv_p1_idx;
static uint32_t csv_p1_sel;
static uint32_t csv_p1_gen_cnt;
static uint32_t csv_p1_sdram_off;

/* Phase 2 持久变量 */
static uint32_t csv_p2_offset;
static uint32_t csv_p2_total_len;
static uint32_t csv_p2_write_cnt;
static uint32_t csv_p2_start_tick;
static FIL     csv_file;

#ifndef __CC_ARM
/* GCC (CMake): .ld NOLOAD 支持，声明为 PLACE_SDRAM 数组 */
static uint8_t g_csv_sdram_buf[CSV_SDRAM_BUF_SIZE] PLACE_SDRAM;
#else
/* ARMCC 5 (Keil): scatter 不能含 SDRAM（__scatterload 提前访问导致 HardFault），用指针 */
static uint8_t * const g_csv_sdram_buf = (uint8_t *)BANK5_SDRAM_ADDR;
#endif

/* 两个 AXISRAM 乒乓缓冲区，各 CSV_CHUNK_SIZE (64KB)，交替使用，合计占用 128KB AXISRAM */
static uint8_t g_csv_buf_A[CSV_CHUNK_SIZE] PLACE_AXI_SRAM;
static uint8_t g_csv_buf_B[CSV_CHUNK_SIZE] PLACE_AXI_SRAM;

/* MDMA 句柄（需文件作用域，中断中需要访问）*/
static MDMA_HandleTypeDef hmdma_csv;
static uint8_t csv_mdma_inited = 0;


/**
 * @brief  MDMA 传输完成回调（中断上下文，仅置标志）
 */
static void csv_mdma_cplt(MDMA_HandleTypeDef *hmdma)
{
    if (hmdma->Instance == MDMA_Channel0)
        g_csv_mdma_flag = 1;
}


/**
 * @brief  一次性初始化 MDMA 外设 + 中断
 * @note   仅在首次 CSV 测试时执行，此后复用
 */
static void csv_mdma_init_one(void)
{
    if (csv_mdma_inited) return;

    __HAL_RCC_MDMA_CLK_ENABLE();

    hmdma_csv.Instance = MDMA_Channel0;
    hmdma_csv.Init.Request = MDMA_REQUEST_SW;
    hmdma_csv.Init.TransferTriggerMode = MDMA_BLOCK_TRANSFER;
    hmdma_csv.Init.Priority = MDMA_PRIORITY_HIGH;
    hmdma_csv.Init.SourceInc = MDMA_SRC_INC_BYTE;
    hmdma_csv.Init.DestinationInc = MDMA_DEST_INC_BYTE;
    hmdma_csv.Init.SourceDataSize = MDMA_SRC_DATASIZE_BYTE;
    hmdma_csv.Init.DestDataSize = MDMA_DEST_DATASIZE_BYTE;
    hmdma_csv.Init.SourceBurst = MDMA_SOURCE_BURST_SINGLE;
    hmdma_csv.Init.DestBurst = MDMA_DEST_BURST_SINGLE;
    hmdma_csv.Init.SourceBlockAddressOffset = 0;
    hmdma_csv.Init.DestBlockAddressOffset = 0;
    HAL_MDMA_Init(&hmdma_csv);

    /* HAL_MDMA_Init 将 XferCpltCallback 清零，需重新注册 */
    hmdma_csv.XferCpltCallback = csv_mdma_cplt;

    HAL_NVIC_SetPriority(MDMA_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(MDMA_IRQn);

    csv_mdma_inited = 1;
}


/* ---- 辅助函数：无符号整数 转 字符串，返回写入指针 ---- */
static char *csv_put_u32(char *p, uint32_t val)
{
    char tmp[12];
    int len = 0;

    if (val == 0)
    {
        *p++ = '0';
        return p;
    }
    while (val)
    {
        tmp[len++] = '0' + (char)(val % 10);
        val /= 10;
    }
    for (int k = len - 1; k >= 0; k--)
        *p++ = tmp[k];
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
 *   Index,Position,Force,Analog,Time,Speed,Voltage
 *   0,0.000,0.000,0,0,0,0.00
 *   1,0.001,0.001,1,1,1,-0.00
 *   ...
 *
 * @param  p  输出指针
 * @param  i  数据点序号 (0 ~ CSV_TEST_POINTS-1)
 * @return    写入结束位置
 */
static char *csv_gen_line(char *p, uint32_t i)
{
    uint32_t q = i / 1000;      /* 整数部分 */
    uint32_t r = i % 1000;      /* 小数部分 */

    /* Index */
    p = csv_put_u32(p, i);           *p++ = ',';

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

    *p++ = '\n';
    return p;
}


/**
 * @brief  CSV 写入测试入口（异步启动，仅初始化状态机）
 *
 * 与旧版本不同，本函数只做初始化并设置 g_csv_state，实际处理
 * 由 sd_csv_tick() 在 ext_hw_test_proc 的每 10ms tick 中完成。
 *
 * 串口指令: $Test,SD,CSV#
 * 代码调用: sd_test_csv()  +  sd_csv_tick() 驱动
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

    /* 一次性初始化 MDMA（硬件 + 中断）*/
    csv_mdma_init_one();

    /* 重置状态机变量 */
    csv_p1_idx = 0;
    csv_p1_sel = 0;
    csv_p1_gen_cnt = 0;
    csv_p1_sdram_off = 0;
    csv_p2_offset = 0;
    csv_p2_write_cnt = 0;
    g_csv_mdma_flag = 1;   /* 初始：无进行中的 MDMA */

    /* 先写 CSV 文件头到 SDRAM 首地址 */
    csv_p1_sdram_off = (uint32_t)sprintf((char *)g_csv_sdram_buf,
        "Index,Position,Force,Analog,Time,Speed,Voltage\n");

    bsp_usart_printf(&s_usart1,
        "[CSV] Phase 1: %lu points simulate -> AXISRAM buf -> SDRAM...\r\n",
        (unsigned long)CSV_TEST_POINTS);

    g_csv_state = CSV_P1_FILL;
}


/* ============================================================================
 * sd_csv_tick — CSV 异步状态机 tick 驱动
 *
 * 由 ext_hw_test_proc 每 10ms 调用一次。每个 tick 执行一个步骤后立即
 * 返回，不阻塞 while(1) 主循环。
 *
 * 流水线示意：
 *
 * Phase 1 (MDMA 中断驱动):
 *   Tick N    CPU 填 buf[sel] → 启 MDMA(buf→SDRAM) → 返回
 *   Tick N+1  MDMA 完成 → CPU 填 buf[1-sel] → 启 MDMA → 返回 ...
 *
 * Phase 2 (f_write IDMA 阻塞写入):
 *   Tick M    memcpy SDRAM→buf → f_write(buf→SD) → 返回
 *   Tick M+1  下一个 64KB ...
 * ============================================================================ */
void sd_csv_tick(void)
{
    if (g_csv_state == CSV_IDLE) return;
    if (g_csv_busy) return;              /* 重入保护 */
    g_csv_busy = 1;

    uint8_t *bufs[2] = { g_csv_buf_A, g_csv_buf_B };

    switch (g_csv_state)
    {
    /* ------------------------------------------------------------------ */
    case CSV_P1_FILL:
    {
        /* 等前一次 MDMA 完成（首块无需等待）*/
        if (csv_p1_gen_cnt > 0)
        {
            if (!g_csv_mdma_flag)
                break;      /* 还没完成，下次 tick 再查 */
        }

        uint32_t sel = csv_p1_sel;

        /* 填满当前 buf */
        char *p = (char *)bufs[sel];
        while (csv_p1_idx < CSV_TEST_POINTS &&
               (uint32_t)(p - (char *)bufs[sel]) < CSV_CHUNK_SIZE - 60)
        {
            p = csv_gen_line(p, csv_p1_idx);
            csv_p1_idx++;
        }
        uint32_t chunk = (uint32_t)(p - (char *)bufs[sel]);

        /* 刷 D-Cache → 启动 MDMA（中断方式）搬运到 SDRAM */
        SCB_CleanDCache_by_Addr((uint32_t *)bufs[sel],
                                (int32_t)((chunk + 31UL) & ~31UL));
        g_csv_mdma_flag = 0;
        HAL_MDMA_Start_IT(&hmdma_csv,
                          (uint32_t)bufs[sel],
                          (uint32_t)(g_csv_sdram_buf + csv_p1_sdram_off),
                          chunk, 1);
        csv_p1_sdram_off += chunk;
        csv_p1_gen_cnt++;
        csv_p1_sel ^= 1;

        if (csv_p1_idx >= CSV_TEST_POINTS)
            g_csv_state = CSV_P2_PREP;
        break;
    }

    /* ------------------------------------------------------------------ */
    case CSV_P2_PREP:
    {
        /* 等最后一块 MDMA 完成 */
        if (!g_csv_mdma_flag) break;

        csv_p2_total_len = csv_p1_sdram_off;

        bsp_usart_printf(&s_usart1,
            "[CSV] Phase 1 done: %lu points -> %lu blocks -> SDRAM %lu bytes\r\n",
            (unsigned long)CSV_TEST_POINTS, (unsigned long)csv_p1_gen_cnt,
            (unsigned long)csv_p2_total_len);

        /* 刷 SDRAM D-Cache，确保 MDMA 写入的数据对外设可见 */
        SCB_CleanDCache_by_Addr((uint32_t *)g_csv_sdram_buf,
                                (int32_t)((csv_p2_total_len + 31) & ~31));
        __DSB();

        /* 打开文件 */
        FRESULT res = f_open(&csv_file, "0:/csv_100k.csv",
                             FA_CREATE_ALWAYS | FA_WRITE);
        if (res != FR_OK)
        {
            bsp_usart_printf(&s_usart1,
                "[CSV] 打开文件失败! res=%d\r\n", res);
            g_csv_state = CSV_IDLE;
            break;
        }

        bsp_usart_printf(&s_usart1,
            "[CSV] Phase 2: SDRAM -> AXISRAM buf -> IDMA -> SD (%lu bytes/block)...\r\n",
            (unsigned long)CSV_CHUNK_SIZE);

        csv_p2_offset = 0;
        csv_p2_write_cnt = 0;
        csv_p2_start_tick = HAL_GetTick();
        g_csv_state = CSV_P2_WRITE;
        break;
    }

    /* ------------------------------------------------------------------ */
    case CSV_P2_WRITE:
    {
        uint32_t sel = csv_p1_sel;
        uint32_t chunk = csv_p2_total_len - csv_p2_offset;
        if (chunk > CSV_CHUNK_SIZE)
            chunk = CSV_CHUNK_SIZE;

        /* SDRAM → AXISRAM（IDMA 必须经过 AXISRAM）*/
        memcpy(bufs[sel], g_csv_sdram_buf + csv_p2_offset, chunk);

        /* f_write 调 disk_write 走 IDMA，等待期间空等，约 1-2ms */
        {
            UINT bw;
            FRESULT res = f_write(&csv_file, bufs[sel], (UINT)chunk, &bw);
            if (res != FR_OK)
            {
                bsp_usart_printf(&s_usart1,
                    "[CSV] 写入失败! offset=%lu res=%d\r\n",
                    (unsigned long)csv_p2_offset, res);
                f_close(&csv_file);
                g_csv_state = CSV_IDLE;
                break;
            }
        }

        csv_p2_offset += chunk;
        csv_p1_sel ^= 1;
        csv_p2_write_cnt++;

        if (csv_p2_offset >= csv_p2_total_len)
        {
            csv_p2_start_tick = HAL_GetTick() - csv_p2_start_tick;
            g_csv_state = CSV_DONE;
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case CSV_DONE:
    {
        uint32_t total_len = csv_p2_total_len;
        uint32_t elapsed_ms = csv_p2_start_tick;
        if (elapsed_ms == 0) elapsed_ms = 1;
        uint32_t speed_kBps =
            (uint32_t)((uint64_t)total_len * 1000UL / 1024UL / elapsed_ms);

        bsp_usart_printf(&s_usart1,
            "========== CSV 写入统计 ==========\r\n"
            "  filename   csv_100k.csv\r\n"
            "  data pts    %lu\r\n"
            "  管道:\r\n"
            "    Phase1:  AXISRAM buf→SDRAM  %lu blocks\r\n"
            "    Phase2:  SDRAM→buf→SD       %lu 次写入\r\n"
            "  文件大小:  %lu bytes (%lu KB)\r\n"
            "  耗时:      %lu ms\r\n"
            "  速度:      %lu KB/s\r\n"
            "==================================\r\n",
            (unsigned long)CSV_TEST_POINTS,
            (unsigned long)csv_p1_gen_cnt,
            (unsigned long)csv_p2_write_cnt,
            (unsigned long)total_len, (unsigned long)(total_len / 1024),
            (unsigned long)elapsed_ms,
            (unsigned long)speed_kBps);

        f_close(&csv_file);
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
 * MDMA 中断入口
 *
 * 启动文件中的弱符号 MDMA_IRQHandler 被此强定义覆盖。
 * ============================================================================ */
void MDMA_IRQHandler(void)
{
    HAL_MDMA_IRQHandler(&hmdma_csv);
}

#endif /* #if 0 — CSV 测试程序暂不启用 */


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
