/**
  ******************************************************************************
  * @file    Main.c
  * @brief   Main program body
  ******************************************************************************
  */

#include "main.h"
#include "bsp_sdio_sd.h"
#include <string.h>

/** USART1 对象定义（非 static，供 stm32f4xx_it.c 使用） */
struct bsp_usart s_usart1;

/* 纯函数 SD 测试 */  
static void sd_pure_test(void)  
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

/**
  * @brief  主函数
  */
int main(void)
{
  /* HAL 库初始化 */
  HAL_Init();

  /* NVIC 优先级分组：2 位抢占 + 2 位子优先级
   * 必须 GROUP_2 才能让 SDIO(0) 优先级高于 DMA(2)，SDIO 工程即如此配置 */
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_2);

  /* 调度器初始化（含系统时钟配置 HSE->PLL->168MHz） */
  scheduler_init();

  /* 初始化 USART1：PA9(TX) / PA10(RX) @115200，DMA2_Stream7(TX) / DMA2_Stream2(RX) */
  bsp_usart_init(&s_usart1, USART1, GPIOA, GPIO_PIN_9, GPIOA, GPIO_PIN_10,
                 115200, DMA2_Stream7, DMA2_Stream2,
                 8, 1024, NULL);

  bsp_usart_send_str(&s_usart1, "USART1 初始化完成\r\n");

  /* 初始化外部 SRAM（FSMC Bank3，0x68000000） */
  bsp_sram_init();
  /* 初始化全部硬件定时器（TIM1~14），TIM6 为 1ms 心跳 */
  scheduler_tim_init();

  {
      int mount_res = sd_mount();
      bsp_usart_printf(&s_usart1, "[DBG] sd_mount() = %d\r\n", mount_res);
  }
  sd_pure_test();

  /* ---- DMA 诊断（使用安全扇区，不破坏文件系统） ---- */
  bsp_usart_send_str(&s_usart1, "\r\n--- DMA diagnostic test ---\r\n");
  sd_dma_full_test(100000);          /* 三路对比：PIO写 + DMA读 + PIO读 */
  sd_dma_read_single_test(100001);   /* 写 PIO 基准 → DMA 读 → 对比 */
  bsp_usart_send_str(&s_usart1, "\r\n");

  /* 主循环：调度器运行（TIM6 中断内轮询 USART + 测试命令） */
  while (1)
  {
    scheduler_run();
  }
}
