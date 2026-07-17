/**
 * @file    AnybusNP40Slave.c
 * @brief   Anybus NP40 模块驱动 (静态实例，函数无参)
 */

#include "AnybusNP40Slave.h"
#include "bsp_usart.h"
#include "bsp_gpio.h"

/* 外部 USART 对象声明 (Main.c) */
extern struct bsp_usart s_usart1;
extern struct bsp_usart s_usart2;

/* DMA 诊断变量 (bsp_usart.c) */
extern uint32_t g_dma_cr_post;
extern uint32_t g_dma_ndtr_post;
extern uint32_t g_dma_m0ar_post;
extern uint32_t g_dma_cr_poll;

/* 复位引脚 GPIO 对象 */
static struct bsp_gpio s_rstPin;

/* 全局实例 */
struct NP40 NP40;

#define NP40_RETRY_MAX         50      /* 50 ticks x 10ms = 500ms */
#define NP40_PROBE_RETRY_MAX   10      /* 最多重试 10 次 (~60s) */


/* ============================================================================
 * CRC16-Modbus 查表法
 * ============================================================================*/
uint16_t NP40_CRC16(const uint8_t *buf, uint8_t len)
{
    static const uint16_t table[256] = {
        0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
        0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
        0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
        0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
        0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
        0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
        0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
        0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
        0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
        0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
        0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
        0xB401,0x74C0,0x7580,0xB541,0x7700,0xB7C1,0xB681,0x7640,0x7200,0xB2C1,0xB381,0x7340,0xB101,0x71C0,0x7080,0xB041,
        0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
        0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
        0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
        0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040
    };
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc = (crc >> 8) ^ table[(crc ^ *buf++) & 0xFF];
    }
    return crc;
}


/* ============================================================================
 * 内部函数前置声明
 * ============================================================================*/
static void Np40_ResendCmd(void);


/* ============================================================================
 * 发送读寄存器命令
 * ============================================================================*/
void NP40_SendReadCmd(uint16_t regAddr)
{
    uint16_t crc;
    uint8_t len = 0;

    NP40.txBuf[len++] = 0x0F;
    NP40.txBuf[len++] = 0x03;
    NP40.txBuf[len++] = (regAddr >> 8) & 0xFF;
    NP40.txBuf[len++] = regAddr & 0xFF;
    NP40.txBuf[len++] = 0x00;
    NP40.txBuf[len++] = 0x01;

    crc = NP40_CRC16(NP40.txBuf, len);
    NP40.txBuf[len++] = crc & 0xFF;
    NP40.txBuf[len++] = (crc >> 8) & 0xFF;

    bsp_usart_send_dma(&s_usart2, NP40.txBuf, len);
}


/* ============================================================================
 * 发送写寄存器命令
 * ============================================================================*/
void NP40_SendWriteCmd(uint16_t regAddr, uint16_t data)
{
    uint16_t crc;
    uint8_t len = 0;

    NP40.txBuf[len++] = 0x0F;
    NP40.txBuf[len++] = 0x10;
    NP40.txBuf[len++] = (regAddr >> 8) & 0xFF;
    NP40.txBuf[len++] = regAddr & 0xFF;
    NP40.txBuf[len++] = 0x00;
    NP40.txBuf[len++] = 0x01;
    NP40.txBuf[len++] = 0x02;
    NP40.txBuf[len++] = (data >> 8) & 0xFF;
    NP40.txBuf[len++] = data & 0xFF;

    crc = NP40_CRC16(NP40.txBuf, len);
    NP40.txBuf[len++] = crc & 0xFF;
    NP40.txBuf[len++] = (crc >> 8) & 0xFF;

    bsp_usart_send_dma(&s_usart2, NP40.txBuf, len);
}


/* ============================================================================
 * 发送 PDO 数据 (0x17 帧)
 * ============================================================================*/
void NP40_SendPDO(const uint8_t *data, uint16_t datalen)
{
    uint16_t readRegs  = NP40_PDO_OUTPUT_SIZE / 2;
    uint16_t writeRegs = NP40_PDO_INPUT_SIZE / 2;
    uint16_t crc;
    uint8_t idx = 0;
    uint16_t i;

    /* 发 PDO 前清空 FIFO 残留 */
    {
        uint8_t dummy[64];
        while (bsp_usart_is_rx_complete(&s_usart2))
            bsp_usart_recv_frame(&s_usart2, dummy, sizeof(dummy));
    }

    NP40.pdo.txBuf[idx++] = 0x0F;
    NP40.pdo.txBuf[idx++] = 0x17;
    NP40.pdo.txBuf[idx++] = 0x0F;
    NP40.pdo.txBuf[idx++] = 0xFD;
    NP40.pdo.txBuf[idx++] = (readRegs >> 8) & 0xFF;
    NP40.pdo.txBuf[idx++] = readRegs & 0xFF;
    NP40.pdo.txBuf[idx++] = 0x00;
    NP40.pdo.txBuf[idx++] = 0x00;
    NP40.pdo.txBuf[idx++] = (writeRegs >> 8) & 0xFF;
    NP40.pdo.txBuf[idx++] = writeRegs & 0xFF;
    NP40.pdo.txBuf[idx++] = NP40_PDO_INPUT_SIZE;

    {
        uint16_t copyLen = (datalen > NP40_PDO_INPUT_SIZE) ? NP40_PDO_INPUT_SIZE : datalen;
        memcpy(&NP40.pdo.txBuf[idx], data, copyLen);
        for (i = copyLen; i < NP40_PDO_INPUT_SIZE; i++)
            NP40.pdo.txBuf[idx + i] = 0;
    }
    idx += NP40_PDO_INPUT_SIZE;

    crc = NP40_CRC16(NP40.pdo.txBuf, idx);
    NP40.pdo.txBuf[idx++] = crc & 0xFF;
    NP40.pdo.txBuf[idx++] = (crc >> 8) & 0xFF;

    bsp_usart_send_dma(&s_usart2, NP40.pdo.txBuf, idx);
}


/* ============================================================================
 * NP40 硬件复位 (非阻塞)
 * ============================================================================*/
void NP40_Reset(void)
{
    NP40_Debug("[NP40] reset start at %lu ms\r\n", HAL_GetTick());

    bsp_gpio_init(&s_rstPin, GPIOE, GPIO_PIN_4, GPIO_MODE_OUTPUT_PP);
    bsp_gpio_set(&s_rstPin, false);  /* PE4=0 */
    NP40_Debug("[NP40] PE4=0 (reset hold 500ms)...\r\n");

    NP40.resetActive = 1;
    NP40.resetPhase = 0;
    NP40.resetTick = HAL_GetTick();
}


/* ============================================================================
 * 命令超时重发
 * ============================================================================*/
static void Np40_ResendCmd(void)
{
    switch (NP40.readType)
    {
        case NP40_READ_DEV_TYPE:
            NP40_SendReadCmd(NP40_REG_DEV_TYPE);
            break;
        case NP40_READ_PROTOCOL:
            NP40_SendReadCmd(NP40_REG_PROTOCOL);
            break;
        case NP40_READ_ADDR_5101:
            NP40_SendWriteCmd(NP40_REG_ADDR_5101, NP40.addr5101);
            break;
        case NP40_READ_ADDR_5102:
            NP40_SendWriteCmd(NP40_REG_ADDR_5102, NP40.addr5102);
            break;
        case NP40_READ_ADDR_5103:
            NP40_SendWriteCmd(NP40_REG_ADDR_5103, NP40.addr5103);
            break;
        case NP40_READ_PDO:
        {
            uint8_t dummy[256];
            while (bsp_usart_is_rx_complete(&s_usart2))
                bsp_usart_recv_frame(&s_usart2, dummy, sizeof(dummy));

            uint8_t buf[NP40_PDO_INPUT_SIZE] = {0};
            buf[0] = NP40.pdo.cntByte0;
            buf[1] = (uint8_t)NP40.pdo.beatState;
            buf[3] = 1;
            NP40_SendPDO(buf, NP40_PDO_INPUT_SIZE);
            break;
        }
    }
    NP40.retry = NP40_RETRY_MAX;
}


/* ============================================================================
 * NP40 主状态机 — 初始化 + PDO 交换
 * ============================================================================*/
void NP40_Task(void)
{
    uint16_t crcCalc, crcRecv;
    uint8_t i;

    /* === DMA 异常自动恢复 === */
    if (NP40.present && s_usart2.dmaing &&
        !(s_usart2.dma_tx->CR & DMA_SxCR_EN) &&
        s_usart2.dma_tx->NDTR != 0)
    {
        static uint32_t sLastDmaWarn = 0;
        uint32_t now = HAL_GetTick();
        if (now - sLastDmaWarn > 1000) {
            sLastDmaWarn = now;
            uint32_t hisr = DMA1->HISR, lisr = DMA1->LISR;
            uint32_t si = ((uint32_t)s_usart2.dma_tx - (uint32_t)DMA1_Stream0) /
                          ((uint32_t)DMA1_Stream1 - (uint32_t)DMA1_Stream0);
            uint32_t st = (si < 4) ? (lisr >> (si * 6)) & 0x1F : (hisr >> ((si - 4) * 6)) & 0x1F;
            NP40_Debug("[NP40 WARN] DMA anomaly: NDTR=%lu flags=0x%02lX\r\n"
                       "  post: CR=0x%08lX poll_CR=0x%08lX\r\n"
                       "  now:  CR=0x%08lX\r\n",
                       (unsigned long)s_usart2.dma_tx->NDTR, (unsigned long)st,
                       (unsigned long)g_dma_cr_post, (unsigned long)g_dma_cr_poll,
                       (unsigned long)s_usart2.dma_tx->CR);
        }
        {
            uint32_t si = ((uint32_t)s_usart2.dma_tx - (uint32_t)DMA1_Stream0) /
                          ((uint32_t)DMA1_Stream1 - (uint32_t)DMA1_Stream0);
            if (si < 4) DMA1->LIFCR = 0x3FUL << (si * 6);
            else        DMA1->HIFCR = 0x3FUL << ((si - 4) * 6);
        }
        CLEAR_BIT(s_usart2.dma_tx->CR, DMA_SxCR_EN);
        s_usart2.dmaing = 0;
        s_usart2.tx_type = 0;
        NP40.retry = NP40_RETRY_MAX;
    }

    /* === PDO 心跳 (10ms/tick) === */
    if (++NP40.pdo.beatCnt >= 20) {
        NP40.pdo.beatCnt = 0;
        NP40.pdo.beatState = (NP40.pdo.beatState == 0) ? 1 : 0;
    }
    if (++NP40.pdo.tickCnt >= 100) {
        NP40.pdo.tickCnt = 0;
        NP40.pdo.cntByte0++;
    }

    /* === 非阻塞复位状态机 === */
    if (NP40.resetActive) {
        if (NP40.resetPhase == 0) {
            if (HAL_GetTick() - NP40.resetTick >= 500) {
                bsp_gpio_set(&s_rstPin, true);
                NP40_Debug("[NP40] PE4=1 (release) at %lu ms\r\n", HAL_GetTick());
                NP40.resetPhase = 1;
                NP40.resetTick = HAL_GetTick();
            }
        } else {
            if (HAL_GetTick() - NP40.resetTick >= 50) {
                NP40.resetActive = 0;
                NP40_Debug("[NP40] reset done, probe at %lu ms\r\n", HAL_GetTick());
                NP40.initTimeout = 100;
                NP40.readType = NP40_READ_DEV_TYPE;
                NP40_SendReadCmd(NP40_REG_DEV_TYPE);
                NP40.retry = NP40_RETRY_MAX;
            }
        }
        return;
    }

    /* === 探活重试 === */
    if (NP40.probeRetryTmo > 0) {
        if (--NP40.probeRetryTmo == 0) {
            if (++NP40.probeRetryCnt >= NP40_PROBE_RETRY_MAX) {
                NP40.probeRetryTmo = 0;
            } else {
                NP40.initTimeout = 100;
                NP40.readType = NP40_READ_DEV_TYPE;
                NP40_SendReadCmd(NP40_REG_DEV_TYPE);
                NP40.retry = NP40_RETRY_MAX;
            }
        }
    }

    /* === 初始化状态机 (init phase) === */
    if (NP40.initTimeout > 0)
    {
        if (bsp_usart_is_rx_complete(&s_usart2)) {
            NP40.rxLen = bsp_usart_recv_frame(&s_usart2, NP40.rxBuf, NP40_RX_BUF_SIZE);
            if (NP40.rxLen == 0) {
                NP40_Debug("[NP40] IDLE noise, keep waiting at %lu ms\r\n", HAL_GetTick());
                NP40.initTimeout = 100;
            } else {
                NP40.initTimeout = 0;
                NP40.present = 1;
                NP40.procPending = 1;
                NP40_Debug("[NP40] Detected! rx_len=%u at %lu ms\r\n", NP40.rxLen, HAL_GetTick());
                NP40_Debug("[NP40]   Raw:");
                for (i = 0; i < NP40.rxLen && i < 16; i++)
                    NP40_Debug(" %02X", NP40.rxBuf[i]);
                NP40_Debug("\r\n");
            }
        } else if (--NP40.initTimeout == 0) {
            NP40.present = 0;
            NP40.procPending = 0;
            NP40.probeRetryTmo = 500;
        } else {
            return;
        }
    }

    if (!NP40.present)
        return;

    /* === 取帧 === */
    if (NP40.procPending) {
        NP40.procPending = 0;
    } else {
        if (!bsp_usart_is_rx_complete(&s_usart2)) {
            if (NP40.retry > 0 && --NP40.retry == 0) {
                if (NP40.readType != NP40_READ_PDO)
                    NP40_Debug("[NP40] Retry cmd (type=%d) at %lu ms\r\n",
                               NP40.readType, HAL_GetTick());
                Np40_ResendCmd();
            }
            return;
        }
        NP40.rxLen = bsp_usart_recv_frame(&s_usart2, NP40.rxBuf, NP40_RX_BUF_SIZE);
    }

    /* === CRC 校验 === */
    if (NP40.rxLen < 3) { NP40.rxLen = 0; return; }
    crcCalc = NP40_CRC16(NP40.rxBuf, NP40.rxLen - 2);
    crcRecv = (uint16_t)(NP40.rxBuf[NP40.rxLen - 2]) |
              (uint16_t)(NP40.rxBuf[NP40.rxLen - 1] << 8);
    if (crcCalc != crcRecv) {
        if (crcRecv != 0x0000) {
            NP40_Debug("[NP40] CRC err: rxLen=%u calc=0x%04X recv=0x%04X\r\n"
                       "  Raw:", NP40.rxLen, crcCalc, crcRecv);
            for (i = 0; i < NP40.rxLen && i < 20; i++)
                NP40_Debug(" %02X", NP40.rxBuf[i]);
            NP40_Debug("\r\n");
        }
        NP40.rxLen = 0;
        return;
    }

    /* === 根据当前阶段处理响应 === */
    switch (NP40.readType)
    {
        case NP40_READ_DEV_TYPE:
        {
            uint16_t devType = (uint16_t)((NP40.rxBuf[3] << 8) | NP40.rxBuf[4]);
            NP40_Debug("[NP40] READ_DEV_TYPE: device=0x%04X at %lu ms\r\n", devType, HAL_GetTick());
            NP40.protocol = NP40_PROTO_ETHERCAT;
            NP40.readType = NP40_READ_ADDR_5101;
            NP40_SendWriteCmd(NP40_REG_ADDR_5101, NP40.addr5101);
            NP40.retry = NP40_RETRY_MAX;
            break;
        }

        case NP40_READ_PROTOCOL:
        {
            NP40.protocol = (NP40.rxLen >= 7) ? NP40.rxBuf[4] : 0;
            NP40.readType = NP40_READ_ADDR_5101;
            NP40_SendWriteCmd(NP40_REG_ADDR_5101, NP40.addr5101);
            NP40.retry = NP40_RETRY_MAX;
            break;
        }

        case NP40_READ_ADDR_5101:
            NP40_Debug("[NP40] WRITE_ADDR_5101 done at %lu ms\r\n", HAL_GetTick());
            NP40.readType = NP40_READ_ADDR_5102;
            NP40_SendWriteCmd(NP40_REG_ADDR_5102, NP40.addr5102);
            NP40.retry = NP40_RETRY_MAX;
            break;

        case NP40_READ_ADDR_5102:
            NP40_Debug("[NP40] WRITE_ADDR_5102 done at %lu ms\r\n", HAL_GetTick());
            NP40.readType = NP40_READ_ADDR_5103;
            NP40_SendWriteCmd(NP40_REG_ADDR_5103, NP40.addr5103);
            NP40.retry = NP40_RETRY_MAX;
            break;

        case NP40_READ_ADDR_5103:
            NP40_Debug("[NP40] WRITE_ADDR_5103 done at %lu ms\r\n", HAL_GetTick());
            NP40.state = NP40_STATE_RUNNING;
            NP40.readType = NP40_READ_PDO;
            NP40_Debug("[NP40] ========== NP40 RUNNING at %lu ms ==========\r\n", HAL_GetTick());

            /* 进入 PDO 前，先清空 FIFO 残留，再立即发送第一条 PDO */
            {
                uint8_t dummy[256];
                while (bsp_usart_is_rx_complete(&s_usart2))
                    bsp_usart_recv_frame(&s_usart2, dummy, sizeof(dummy));
            }
            {
                uint8_t buf[NP40_PDO_INPUT_SIZE] = {0};
                buf[0] = NP40.pdo.cntByte0;
                buf[1] = (uint8_t)NP40.pdo.beatState;
                buf[3] = 1;
                NP40_SendPDO(buf, NP40_PDO_INPUT_SIZE);
            }

            NP40.retry = 5;
            break;

        case NP40_READ_PDO:
        {
            uint8_t outLen = (NP40.rxLen >= 5) ? NP40.rxBuf[2] : 0;
            uint8_t copyLen = (outLen > NP40_PDO_OUTPUT_SIZE) ? NP40_PDO_OUTPUT_SIZE : outLen;

            for (i = 0; i < copyLen; i += 2) {
                NP40.pdo.output[i]     = (i + 1 < copyLen) ? NP40.rxBuf[4 + i] : NP40.rxBuf[3 + i];
                NP40.pdo.output[i + 1] = NP40.rxBuf[3 + i];
            }
            for (i = copyLen; i < NP40_PDO_OUTPUT_SIZE; i++)
                NP40.pdo.output[i] = 0;

            NP40.pdo.plcConnected = (copyLen > 7) ? (NP40.pdo.output[7] == 1) : 0;
            NP40.pdo.plcHbCnt     = (copyLen > 8) ? NP40.pdo.output[8] : 0;

            /* 打印 (每 50 次 = 500ms) */
            {
                static uint8_t sPdoPrintCnt = 0;
                if (++sPdoPrintCnt >= 50) {
                    sPdoPrintCnt = 0;
                    NP40_Debug(
                        "[NP40 PDO] TX[0..3]: %02X %02X %02X %02X  "
                        "RX[0..3]: %02X %02X %02X %02X  "
                        "plc_conn=%u hb=%u\r\n",
                        NP40.pdo.txBuf[10], NP40.pdo.txBuf[11],
                        NP40.pdo.txBuf[12], NP40.pdo.txBuf[13],
                        NP40.pdo.output[0], NP40.pdo.output[1],
                        NP40.pdo.output[2], NP40.pdo.output[3],
                        (unsigned)NP40.pdo.plcConnected,
                        (unsigned)NP40.pdo.plcHbCnt);
                }
            }

            NP40.retry = 5;
            break;
        }
    }

    NP40.rxLen = 0;
}


/* ============================================================================
 * NP40 初始化
 * ============================================================================*/
void NP40_Init(void)
{
    NP40.present   = 0;
    NP40.state     = NP40_STATE_IDLE;
    NP40.protocol  = 0;
    NP40.readType  = NP40_READ_DEV_TYPE;
    NP40.rxLen     = 0;

    NP40.procPending   = 0;
    NP40.retry         = 0;
    NP40.initTimeout   = 0;
    NP40.probeRetryCnt = 0;
    NP40.probeRetryTmo = 0;

    NP40.resetActive = 0;
    NP40.resetPhase  = 0;
    NP40.resetTick   = 0;

    NP40.pdo.beatState = 0;
    NP40.pdo.beatCnt   = 0;
    NP40.pdo.tickCnt   = 0;
    NP40.pdo.cntByte0  = 0;

    NP40.pdo.plcConnected = 0;
    NP40.pdo.plcHbCnt     = 0;

    NP40.addr5101 = 0;
    NP40.addr5102 = 0x0080;
    NP40.addr5103 = 0x0080;

    { uint8_t _i; for (_i = 0; _i < NP40_PDO_OUTPUT_SIZE; _i++) NP40.pdo.output[_i] = 0; }

    NP40_Debug("\r\n========== [NP40] Init Start ==========\r\n");
    NP40_Reset();
    NP40_Debug("[NP40] Reset started (non-blocking)\r\n");
}


/* ============================================================================
 * 状态查询
 * ============================================================================*/
uint8_t NP40_GetState(void)         { return NP40.state; }
uint8_t NP40_GetProtocol(void)      { return NP40.protocol; }
uint8_t NP40_GetPdoByte0(void)      { return NP40.pdo.cntByte0; }
uint8_t NP40_GetPdoByte1(void)      { return (uint8_t)NP40.pdo.beatState; }
uint8_t NP40_GetOutputByte0(void)   { return NP40.pdo.output[0]; }
uint8_t NP40_GetPlcConnected(void)  { return NP40.pdo.plcConnected; }
uint16_t NP40_GetPlcHbCnt(void)     { return NP40.pdo.plcHbCnt; }
