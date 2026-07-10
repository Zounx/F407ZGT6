/**
 * @file    Profinet.c
 * @brief   PROFINET — Anybus NP40 模块驱动 (移植版)
 *
 * 从 test_tx 工程移植，适配 F407ZGT6 的 bsp_usart + bsp_gpio 驱动
 *
 * 适配要点:
 *   SCIB_BufTx        → bsp_usart_send_dma(&s_usart2, ...)
 *   SCIB_IsRxComplete → bsp_usart_is_rx_complete(&s_usart2)
 *   SCIB_BufRx        → bsp_usart_recv_frame(&s_usart2, ...)
 *   SCI_Printf        → bsp_usart_printf(&s_usart1, ...)
 *   GetTick           → HAL_GetTick
 *   GPIO_Set+reset    → bsp_gpio_init + bsp_gpio_set
 */

#include "Profinet.h"
#include "bsp_usart.h"
#include "bsp_gpio.h"

/* ============================================================================
 * 外部 USART 对象声明 (在 Main.c 中定义)
 * ============================================================================ */
extern struct bsp_usart  s_usart1;    /* 调试串口 */
extern struct bsp_usart  s_usart2;    /* NP40 Profinet (USART2) */

/* ============================================================================
 * 内部变量
 * ============================================================================ */

/* 接收缓冲区 */
static uint8_t s_rx_buf[PN_RX_BUF_SIZE];
static uint16_t s_rx_len = 0;

/* 发送缓冲区 */
static uint8_t s_tx_buf[PN_TX_BUF_SIZE];

/* 状态 */
static PN_State s_state = PN_STATE_IDLE;
static uint8_t s_protocol = 0;
static PN_ReadType s_read_type = PN_READ_DEV_TYPE;

/* NP40 检测 */
static uint8_t  s_np40_present = 0;     /* 1=已检测到 NP40 */
static uint16_t s_init_timeout = 0;     /* 初始化超时计数 (ticks) */
/* NP40 探测重试 */
#define PN_PROBE_RETRY_MAX     10      /* 最多重试 10 次 (约 60s) */
static uint8_t  s_probe_retry_cnt = 0; /* 当前重试次数 */
static uint16_t s_probe_retry_tmo = 0;  /* 探活重试延时计数 (ticks, 超时后 5s 重试) */

/* PDO 数据尺寸 (128 输入 / 128 输出, 与 NP40 配置一致) */
#define PN_PDO_INPUT_SIZE   128  /* 我们发给 NP40 的字节数 (64 regs) */
#define PN_PDO_OUTPUT_SIZE  128  /* NP40 返回给我们的字节数 (64 regs) */

/* PDO 心跳 */
static uint16_t  s_pdo_beat_state = 0;
static uint8_t   s_pdo_beat_cnt = 0;
static uint16_t  s_pdo_tick_cnt = 0;   /* 计数器 tick (10ms/tick) */
static uint8_t   s_pdo_cnt_byte0 = 0;  /* → byte0, 每 1s +1 */

/* OutputPDO (NP40返回数据) 解析缓存 */
static uint8_t  s_output_pdo[PN_PDO_OUTPUT_SIZE];
static uint8_t  s_plc_connected = 0;
static uint16_t s_plc_hb_cnt = 0;

/* NP40 配置地址 */
static uint16_t s_addr_5101 = 0;
static uint16_t s_addr_5102 = 0x0080;
static uint16_t s_addr_5103 = 0x0080;

/* 标记: init 阶段已消费一帧 */
static uint8_t s_proc_pending = 0;

/* 命令重试: 每次发命令后复位, PN_Task 等待超时后重发 */
static uint16_t s_retry = 0;
#define PN_RETRY_MAX 50   /* 50 ticks × 10ms = 500ms 超时重发 */

/* NP40 非阻塞复位状态机 */
static uint8_t  s_reset_active = 0;
static uint8_t  s_reset_phase = 0;
static uint32_t s_reset_tick = 0;

/* NP40 复位 GPIO 对象 */
static struct bsp_gpio s_np40_rst;

/* PDO 发送缓冲区 */
static uint8_t s_pdo_tx_buf[256];

/* ============================================================================
 * CRC16-Modbus 查表法
 * ============================================================================ */
uint16_t PN_CRC16(uint8_t *buf, uint8_t len)
{
    static const uint8_t crc_hi[] = {
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40,0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,
        0x00,0xC1,0x81,0x40,0x01,0xC0,0x80,0x41,0x01,0xC0,0x80,0x41,0x00,0xC1,0x81,0x40
    };
    static const uint8_t crc_lo[] = {
        0x00,0xC0,0xC1,0x01,0xC3,0x03,0x02,0xC2,0xC6,0x06,0x07,0xC7,0x05,0xC5,0xC4,0x04,
        0xCC,0x0C,0x0D,0xCD,0x0F,0xCF,0xCE,0x0E,0x0A,0xCA,0xCB,0x0B,0xC9,0x09,0x08,0xC8,
        0xD8,0x18,0x19,0xD9,0x1B,0xDB,0xDA,0x1A,0x1E,0xDE,0xDF,0x1F,0xDD,0x1D,0x1C,0xDC,
        0x14,0xD4,0xD5,0x15,0xD7,0x17,0x16,0xD6,0xD2,0x12,0x13,0xD3,0x11,0xD1,0xD0,0x10,
        0xF0,0x30,0x31,0xF1,0x33,0xF3,0xF2,0x32,0x36,0xF6,0xF7,0x37,0xF5,0x35,0x34,0xF4,
        0x3C,0xFC,0xFD,0x3D,0xFF,0x3F,0x3E,0xFE,0xFA,0x3A,0x3B,0xFB,0x39,0xF9,0xF8,0x38,
        0x28,0xE8,0xE9,0x29,0xEB,0x2B,0x2A,0xEA,0xEE,0x2E,0x2F,0xEF,0x2D,0xED,0xEC,0x2C,
        0xE4,0x24,0x25,0xE5,0x27,0xE7,0xE6,0x26,0x22,0xE2,0xE3,0x23,0xE1,0x21,0x20,0xE0,
        0xA0,0x60,0x61,0xA1,0x63,0xA3,0xA2,0x62,0x66,0xA6,0xA7,0x67,0xA5,0x65,0x64,0xA4,
        0x6C,0xAC,0xAD,0x6D,0xAF,0x6F,0x6E,0xAE,0xAA,0x6A,0x6B,0xAB,0x69,0xA9,0xA8,0x68,
        0x78,0xB8,0xB9,0x79,0xBB,0x7B,0x7A,0xBA,0xBE,0x7E,0x7F,0xBF,0x7D,0xBD,0xBC,0x7C,
        0xB4,0x74,0x75,0xB5,0x77,0xB7,0xB6,0x76,0x72,0xB2,0xB3,0x73,0xB1,0x71,0x70,0xB0,
        0x50,0x90,0x91,0x51,0x93,0x53,0x52,0x92,0x96,0x56,0x57,0x97,0x55,0x95,0x94,0x54,
        0x9C,0x5C,0x5D,0x9D,0x5F,0x9F,0x9E,0x5E,0x5A,0x9A,0x9B,0x5B,0x99,0x59,0x58,0x98,
        0x88,0x48,0x49,0x89,0x4B,0x8B,0x8A,0x4A,0x4E,0x8E,0x8F,0x4F,0x8D,0x4D,0x4C,0x8C,
        0x44,0x84,0x85,0x45,0x87,0x47,0x46,0x86,0x82,0x42,0x43,0x83,0x41,0x81,0x80,0x40
    };
    uint8_t hi = 0xFF, lo = 0xFF, idx;
    while (len--) {
        idx = hi ^ *buf++;
        hi = lo ^ crc_hi[idx];
        lo = crc_lo[idx];
    }
    return (uint16_t)((hi << 8) | lo);
}

/* ============================================================================
 * 发送读寄存器命令
 * 格式: 0x0F 0x03 ADDR_H ADDR_L 0x00 0x01 CRC_H CRC_L
 * ============================================================================ */
void PN_SendReadCmd(uint16_t reg_addr)
{
    uint16_t crc;
    uint8_t len = 0;

    s_tx_buf[len++] = 0x0F;
    s_tx_buf[len++] = 0x03;
    s_tx_buf[len++] = (reg_addr >> 8) & 0xFF;
    s_tx_buf[len++] = reg_addr & 0xFF;
    s_tx_buf[len++] = 0x00;
    s_tx_buf[len++] = 0x01;

    crc = PN_CRC16(s_tx_buf, len);
    s_tx_buf[len++] = (crc >> 8) & 0xFF;
    s_tx_buf[len++] = crc & 0xFF;

    bsp_usart_send_dma(&s_usart2, s_tx_buf, len);
}

/* ============================================================================
 * 发送写寄存器命令
 * 格式: 0x0F 0x10 ADDR_H ADDR_L 0x00 0x01 0x02 DATA_H DATA_L CRC_H CRC_L
 * ============================================================================ */
void PN_SendWriteCmd(uint16_t reg_addr, uint16_t data)
{
    uint16_t crc;
    uint8_t len = 0;

    s_tx_buf[len++] = 0x0F;
    s_tx_buf[len++] = 0x10;
    s_tx_buf[len++] = (reg_addr >> 8) & 0xFF;
    s_tx_buf[len++] = reg_addr & 0xFF;
    s_tx_buf[len++] = 0x00;
    s_tx_buf[len++] = 0x01;
    s_tx_buf[len++] = 0x02;
    s_tx_buf[len++] = (data >> 8) & 0xFF;
    s_tx_buf[len++] = data & 0xFF;

    crc = PN_CRC16(s_tx_buf, len);
    s_tx_buf[len++] = (crc >> 8) & 0xFF;
    s_tx_buf[len++] = crc & 0xFF;

    bsp_usart_send_dma(&s_usart2, s_tx_buf, len);
}

/* ============================================================================
 * 发送 PDO 数据
 * 0x17 帧: 11报头 + 128数据 + 2校验
 * ============================================================================ */
void PN_SendPDO(uint8_t *data, uint16_t datalen)
{
    uint16_t read_regs  = PN_PDO_OUTPUT_SIZE / 2;         /* 128B = 64 reg */
    uint16_t write_regs = PN_PDO_INPUT_SIZE / 2;          /* 128B = 64 reg */
    uint16_t crc;
    uint8_t idx = 0;
    uint16_t i;

    /* 发PDO前先清空FIFO残留数据, 防止旧帧被当作本次响应 */
    {
        uint8_t dummy[64];
        while (bsp_usart_is_rx_complete(&s_usart2))
            bsp_usart_recv_frame(&s_usart2, dummy, sizeof(dummy));
    }

    s_pdo_tx_buf[idx++] = 0x0F;
    s_pdo_tx_buf[idx++] = 0x17;
    s_pdo_tx_buf[idx++] = 0x0F;
    s_pdo_tx_buf[idx++] = 0xFD;
    s_pdo_tx_buf[idx++] = (read_regs >> 8) & 0xFF;
    s_pdo_tx_buf[idx++] = read_regs & 0xFF;
    s_pdo_tx_buf[idx++] = 0x00;
    s_pdo_tx_buf[idx++] = 0x00;
    s_pdo_tx_buf[idx++] = (write_regs >> 8) & 0xFF;
    s_pdo_tx_buf[idx++] = write_regs & 0xFF;
    s_pdo_tx_buf[idx++] = PN_PDO_INPUT_SIZE;   /* 固定 128 字节数据 */

    uint16_t copy_len = (datalen > PN_PDO_INPUT_SIZE) ? PN_PDO_INPUT_SIZE : datalen;
    memcpy(&s_pdo_tx_buf[idx], data, copy_len);
    for (i = copy_len; i < PN_PDO_INPUT_SIZE; i++)
        s_pdo_tx_buf[idx + i] = 0;              /* 剩余部分零填充 */
    idx += PN_PDO_INPUT_SIZE;

    crc = PN_CRC16(s_pdo_tx_buf, idx);
    s_pdo_tx_buf[idx++] = (crc >> 8) & 0xFF;
    s_pdo_tx_buf[idx++] = crc & 0xFF;

    bsp_usart_send_dma(&s_usart2, s_pdo_tx_buf, idx);
}

/* ============================================================================
 * NP40 硬件复位 (非阻塞版)
 * PE4 拉低 500ms 再拉高
 * ============================================================================ */
void PN_Reset(void)
{
    bsp_usart_printf(&s_usart1, "[PN] NP40 reset start at %lu ms\r\n", HAL_GetTick());

    /* 初始化 PE4 为推挽输出 (bsp_gpio 自动处理时钟使能) */
    bsp_gpio_init(&s_np40_rst, GPIOE, GPIO_PIN_4, GPIO_MODE_OUTPUT_PP);

    bsp_gpio_set(&s_np40_rst, false);  /* PE4=0, 复位 */
    bsp_usart_printf(&s_usart1, "[PN] NP40 PE4=0 (reset hold 500ms)...\r\n");

    /* 非阻塞: 状态机接管, 立即返回 */
    s_reset_active = 1;
    s_reset_phase = 0;
    s_reset_tick = HAL_GetTick();
}

/* ============================================================================
 * PN 初始化
 * 1. 硬件复位 NP40 (非阻塞, 状态机在 PN_Task)
 * 2. SCIB (USART2, PA2/TX PA3/RX) 由外部初始化 (Main.c)
 * 3. 状态机复位完成后自动发探测命令
 * ============================================================================ */
void PN_Init(void)
{
    s_np40_present = 0;
    s_state = PN_STATE_IDLE;
    s_rx_len = 0;
    s_pdo_beat_state = 0;
    s_pdo_beat_cnt = 0;
    s_pdo_tick_cnt = 0;
    s_pdo_cnt_byte0 = 0;
    s_plc_connected = 0;
    s_plc_hb_cnt = 0;
    { uint8_t _i; for (_i = 0; _i < PN_PDO_OUTPUT_SIZE; _i++) s_output_pdo[_i] = 0; }
    s_proc_pending = 0;
    s_retry = 0;
    s_probe_retry_tmo = 0;
    s_probe_retry_cnt = 0;
    s_reset_active = 0;
    s_init_timeout = 0;

    bsp_usart_printf(&s_usart1, "\r\n========== [PN] NP40 Profinet Module Init Start ==========\r\n");
    bsp_usart_printf(&s_usart1, "[PN] USART2 (NP40) initialized, baudrate=115200\r\n");

    /* 非阻塞复位 (状态机在 PN_Task 中) */
    PN_Reset();
    bsp_usart_printf(&s_usart1, "[PN] Reset started (non-blocking), system init continues...\r\n");
}

/* ============================================================================
 * 命令超时重发
 * ============================================================================ */
static void PN_ResendCmd(void)
{
    switch (s_read_type)
    {
        case PN_READ_DEV_TYPE:    PN_SendReadCmd(PN_REG_DEV_TYPE);      break;
        case PN_READ_PROTOCOL:    PN_SendReadCmd(PN_REG_PROTOCOL);      break;
        case PN_READ_ADDR_5101:   PN_SendWriteCmd(PN_REG_ADDR_5101, s_addr_5101); break;
        case PN_READ_ADDR_5102:   PN_SendWriteCmd(PN_REG_ADDR_5102, s_addr_5102); break;
        case PN_READ_ADDR_5103:   PN_SendWriteCmd(PN_REG_ADDR_5103, s_addr_5103); break;
        case PN_READ_PDO:
            {
                /* 发PDO前清空FIFO残留数据 */
                uint8_t dummy[256];
                while (bsp_usart_is_rx_complete(&s_usart2))
                    bsp_usart_recv_frame(&s_usart2, dummy, sizeof(dummy));

                uint8_t buf[PN_PDO_INPUT_SIZE] = {0};
                buf[0] = s_pdo_cnt_byte0;   /* byte0: 每1s+1计数器 */
                buf[1] = (uint8_t)s_pdo_beat_state;  /* byte1: 200ms翻转 */
                buf[3] = 1;                 /* byte2: 常驻1, PLC连接确认用 */

                PN_SendPDO(buf, PN_PDO_INPUT_SIZE);
            }
            break;
    }
    s_retry = PN_RETRY_MAX;
}

/* ============================================================================
 * PN 周期处理 — 在 10ms 定时器或调度器中调用
 * ============================================================================ */
void PN_Task(void)
{
    uint16_t crc_calc, crc_recv;
    uint8_t i;

    /* === 检测 & 自动恢复 DMA 异常态 (EN=0, dmaing=1, NDTR!=0) === */
    if (s_np40_present && s_usart2.dmaing &&
        !(s_usart2.dma_tx->CR & DMA_SxCR_EN) &&
        s_usart2.dma_tx->NDTR != 0)
    {
        static uint32_t s_last_dma_warn = 0;
        uint32_t now = HAL_GetTick();
        if (now - s_last_dma_warn > 1000)
        {
            s_last_dma_warn = now;
            bsp_usart_printf(&s_usart1, "[PN WARN] DMA anomaly: dmaing=1 EN=0 NDTR=%lu, auto-recover\r\n",
                (unsigned long)s_usart2.dma_tx->NDTR);
        }
        /* 自动恢复：清标志 + 停 DMA */
        {
            uint32_t stream_idx = ((uint32_t)s_usart2.dma_tx - (uint32_t)DMA1_Stream0) /
                                  ((uint32_t)DMA1_Stream1 - (uint32_t)DMA1_Stream0);
            if (stream_idx < 4)
                DMA1->LIFCR = 0x3FUL << (stream_idx * 6);
            else
                DMA1->HIFCR = 0x3FUL << ((stream_idx - 4) * 6);
        }
        CLEAR_BIT(s_usart2.dma_tx->CR, DMA_SxCR_EN);
        s_usart2.dmaing = 0;
        s_usart2.tx_type = 0;
        s_retry = PN_RETRY_MAX;  /* 异常恢复后重发命令，避免 PN_Task 永久等待 */
    }

    /* === 独立运行: PDO 心跳计数 (10ms/tick) === */
    /* 200ms 翻转 → byte1 */
    if (++s_pdo_beat_cnt >= 20) {
        s_pdo_beat_cnt = 0;
        s_pdo_beat_state = (s_pdo_beat_state == 0) ? 1 : 0;
    }
    /* 100 tick (1s): byte0 +1 */
    if (++s_pdo_tick_cnt >= 100) {
        s_pdo_tick_cnt = 0;
        s_pdo_cnt_byte0++;
    }

    /* === NP40 非阻塞复位状态机 === */
    if (s_reset_active) {
        if (s_reset_phase == 0) {
            /* Phase 0: PE4=0, 等待 500ms 后释放复位 */
            if (HAL_GetTick() - s_reset_tick >= 500) {
                bsp_gpio_set(&s_np40_rst, true);  /* PE4=1 */
                bsp_usart_printf(&s_usart1, "[PN] NP40 PE4=1 (reset release) at %lu ms\r\n", HAL_GetTick());
                s_reset_phase = 1;
                s_reset_tick = HAL_GetTick();
            }
        } else {
            /* Phase 1: PE4=1, 等待短延时后开始探活 */
            if (HAL_GetTick() - s_reset_tick >= 50) {
                s_reset_active = 0;
                bsp_usart_printf(&s_usart1, "[PN] NP40 reset done at %lu ms, starting probe\r\n", HAL_GetTick());
                s_init_timeout = 100;
                s_read_type = PN_READ_DEV_TYPE;
                bsp_usart_printf(&s_usart1, "[PN] Sending probe: read reg 0x%04X (device type) at %lu ms\r\n",
                           PN_REG_DEV_TYPE, HAL_GetTick());
                PN_SendReadCmd(PN_REG_DEV_TYPE);
                s_retry = PN_RETRY_MAX;
            }
        }
        return;
    }

    /* === 探活重试 === */
    if (s_probe_retry_tmo > 0) {
        if (--s_probe_retry_tmo == 0) {
            if (++s_probe_retry_cnt >= PN_PROBE_RETRY_MAX) {
                s_probe_retry_tmo = 0;
            } else {
                s_init_timeout = 100;
                s_read_type = PN_READ_DEV_TYPE;
                PN_SendReadCmd(PN_REG_DEV_TYPE);
                s_retry = PN_RETRY_MAX;
            }
        }
    }

    if (s_init_timeout > 0)
    {
        if (bsp_usart_is_rx_complete(&s_usart2)) {
            s_rx_len = bsp_usart_recv_frame(&s_usart2, s_rx_buf, PN_RX_BUF_SIZE);

            if (s_rx_len == 0) {
                bsp_usart_printf(&s_usart1, "[PN] IDLE noise (rx_len=0), keep waiting at %lu ms\r\n",
                           HAL_GetTick());
                s_init_timeout = 100;
            } else {
                s_init_timeout = 0;
                s_np40_present = 1;
                s_proc_pending = 1;
                bsp_usart_printf(&s_usart1, "[PN] NP40 Detected! rx_len=%u at %lu ms\r\n", s_rx_len, HAL_GetTick());
                bsp_usart_printf(&s_usart1, "[PN]   Raw data:");
                for (i = 0; i < s_rx_len && i < 16; i++)
                    bsp_usart_printf(&s_usart1, " %02X", s_rx_buf[i]);
                bsp_usart_printf(&s_usart1, "\r\n");
            }
        } else if (--s_init_timeout == 0) {
            s_np40_present = 0;
            s_proc_pending = 0;
            s_probe_retry_tmo = 500;    /* 5 秒后自动重试 */
        } else {
            return;
        }
    }

    if (!s_np40_present)
        return;

    /* === 从 FIFO 取下一帧 === */
    if (s_proc_pending) {
        s_proc_pending = 0;
    } else {
        if (!bsp_usart_is_rx_complete(&s_usart2)) {
            if (s_retry > 0 && --s_retry == 0) {
                if (s_read_type == PN_READ_PDO) {
                    /* PDO 超时静默重发 */
                } else
                    bsp_usart_printf(&s_usart1, "[PN] Retry cmd (type=%d) at %lu ms\r\n",
                               s_read_type, HAL_GetTick());
                PN_ResendCmd();
            }
            return;
        }
        s_rx_len = bsp_usart_recv_frame(&s_usart2, s_rx_buf, PN_RX_BUF_SIZE);
    }

    /* === CRC 校验 === */
    if (s_rx_len < 3)
    {
        s_rx_len = 0;
        return;
    }

    crc_calc = PN_CRC16(s_rx_buf, s_rx_len - 2);
    crc_recv = (uint16_t)((s_rx_buf[s_rx_len - 2] << 8) | s_rx_buf[s_rx_len - 1]);

    if (crc_calc != crc_recv)
    {
        if (crc_recv != 0x0000) {
            bsp_usart_printf(&s_usart1, "[PN] CRC err: calc=0x%04X recv=0x%04X\r\n", crc_calc, crc_recv);
        }
        s_rx_len = 0;
        return;
    }

    /* === 根据当前读类型处理响应 === */
    switch (s_read_type)
    {
        case PN_READ_DEV_TYPE:
        {
            uint16_t dev_type = (uint16_t)((s_rx_buf[3] << 8) | s_rx_buf[4]);
            bsp_usart_printf(&s_usart1, "[PN] PN_READ_DEV_TYPE: device=0x%04X at %lu ms\r\n",
                       dev_type, HAL_GetTick());
            /*
             * 跳过读协议类型 (0x5004)：部分 NP40 固件版本（如 EtherCAT）
             * 不响应此寄存器，不影响后续配置。直接默认协议类型。
             */
            s_protocol = PN_PROTO_ETHERCAT;  /* 默认 EtherCAT */
            s_read_type = PN_READ_ADDR_5101;
            bsp_usart_printf(&s_usart1, "[PN]   -> Skip 0x5004 protocol read\r\n");
            bsp_usart_printf(&s_usart1, "[PN]   -> Next: write reg 0x%04X = 0x%04X\r\n",
                       PN_REG_ADDR_5101, s_addr_5101);
            PN_SendWriteCmd(PN_REG_ADDR_5101, s_addr_5101);
            s_retry = PN_RETRY_MAX;
            break;
        }

        case PN_READ_PROTOCOL:
        {
            if (s_rx_len >= 7)
                s_protocol = s_rx_buf[4];
            else
                s_protocol = 0;

            const char *proto_str = "UNKNOWN";
            switch (s_protocol) {
                case PN_PROTO_PROFINET:  proto_str = "PROFINET";  break;
                case PN_PROTO_ETHERIP:   proto_str = "EtherNet/IP"; break;
                case PN_PROTO_ETHERCAT:  proto_str = "EtherCAT";  break;
                case PN_PROTO_MODBUSTCP: proto_str = "Modbus TCP"; break;
            }
            bsp_usart_printf(&s_usart1, "[PN] PN_READ_PROTOCOL: protocol=0x%02X (%s) at %lu ms\r\n",
                       s_protocol, proto_str, HAL_GetTick());

            s_read_type = PN_READ_ADDR_5101;
            bsp_usart_printf(&s_usart1, "[PN]   -> Next: write reg 0x%04X = 0x%04X\r\n",
                       PN_REG_ADDR_5101, s_addr_5101);
            PN_SendWriteCmd(PN_REG_ADDR_5101, s_addr_5101);
            s_retry = PN_RETRY_MAX;
            break;
        }

        case PN_READ_ADDR_5101:
            bsp_usart_printf(&s_usart1, "[PN] PN_WRITE_ADDR_5101 done at %lu ms\r\n", HAL_GetTick());
            s_read_type = PN_READ_ADDR_5102;
            bsp_usart_printf(&s_usart1, "[PN]   -> Next: write reg 0x%04X = 0x%04X\r\n",
                       PN_REG_ADDR_5102, s_addr_5102);
            PN_SendWriteCmd(PN_REG_ADDR_5102, s_addr_5102);
            s_retry = PN_RETRY_MAX;
            break;

        case PN_READ_ADDR_5102:
            bsp_usart_printf(&s_usart1, "[PN] PN_WRITE_ADDR_5102 done at %lu ms\r\n", HAL_GetTick());
            s_read_type = PN_READ_ADDR_5103;
            bsp_usart_printf(&s_usart1, "[PN]   -> Next: write reg 0x%04X = 0x%04X\r\n",
                       PN_REG_ADDR_5103, s_addr_5103);
            PN_SendWriteCmd(PN_REG_ADDR_5103, s_addr_5103);
            s_retry = PN_RETRY_MAX;
            break;

        case PN_READ_ADDR_5103:
            s_state = PN_STATE_RUNNING;
            s_read_type = PN_READ_PDO;
            bsp_usart_printf(&s_usart1, "[PN] PN_WRITE_ADDR_5103 done at %lu ms\r\n", HAL_GetTick());
            bsp_usart_printf(&s_usart1, "[PN] ========== NP40 Configured & RUNNING at %lu ms ==========\r\n",
                       HAL_GetTick());
            s_retry = 5;
            break;

        case PN_READ_PDO:
            {
                uint8_t out_len = (s_rx_len >= 5) ? s_rx_buf[2] : 0;
                uint8_t copy_len = (out_len > PN_PDO_OUTPUT_SIZE) ? PN_PDO_OUTPUT_SIZE : out_len;

                /* 复制 OutputPDO 数据 — 字节交换 */
                for (i = 0; i < copy_len; i += 2) {
                    s_output_pdo[i]     = (i + 1 < copy_len) ? s_rx_buf[4 + i] : s_rx_buf[3 + i];
                    s_output_pdo[i + 1] = s_rx_buf[3 + i];
                }
                for (i = copy_len; i < PN_PDO_OUTPUT_SIZE; i++)
                    s_output_pdo[i] = 0;

                /* 提取状态 */
                #define PN_OUTPUT_DATA_OFFSET  4
                #define PN_PLC_CONN_IDX        (PN_OUTPUT_DATA_OFFSET + 3)
                #define PN_PLC_HB_IDX          (PN_OUTPUT_DATA_OFFSET + 4)
                s_plc_connected = (copy_len > PN_PLC_CONN_IDX) ? (s_output_pdo[PN_PLC_CONN_IDX] == 1) : 0;
                s_plc_hb_cnt    = (copy_len > PN_PLC_HB_IDX) ? s_output_pdo[PN_PLC_HB_IDX] : 0;

                /* PDO 数据打印（每 50 次 = 500ms 一次） */
                {
                    static uint8_t s_pdo_print_cnt = 0;
                    if (++s_pdo_print_cnt >= 50) {
                        s_pdo_print_cnt = 0;
                        bsp_usart_printf(&s_usart1,
                            "[PN PDO] TX[0..3]: %02X %02X %02X %02X  "
                            "RX[0..3]: %02X %02X %02X %02X  "
                            "plc_conn=%u hb=%u\r\n",
                            s_pdo_tx_buf[10],             /* buf[0] = cnt_byte0 */
                            s_pdo_tx_buf[11],             /* buf[1] = beat_state */
                            s_pdo_tx_buf[12],             /* buf[2] */
                            s_pdo_tx_buf[13],             /* buf[3] = 1 */
                            s_output_pdo[0], s_output_pdo[1],
                            s_output_pdo[2], s_output_pdo[3],
                            (unsigned)s_plc_connected,
                            (unsigned)s_plc_hb_cnt);
                    }
                }
            }
            s_retry = 5;
            break;
    }

    s_rx_len = 0;
}

/* ============================================================================
 * 获取状态/数据接口
 * ============================================================================ */
PN_State PN_GetState(void)
{
    return s_state;
}

uint8_t PN_GetProtocol(void)
{
    return s_protocol;
}

uint8_t PN_GetPdoByte0(void)
{
    return s_pdo_cnt_byte0;
}

uint8_t PN_GetPdoByte1(void)
{
    return (uint8_t)s_pdo_beat_state;
}

uint8_t PN_GetOutputByte0(void)
{
    return s_output_pdo[0];
}

uint8_t PN_GetPlcConnected(void)
{
    return s_plc_connected;
}

uint16_t PN_GetPlcHbCnt(void)
{
    return s_plc_hb_cnt;
}
