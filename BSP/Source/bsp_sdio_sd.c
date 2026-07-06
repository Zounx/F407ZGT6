/**
 ****************************************************************************************************
 * @file        bsp_sdio_sd.c
 * @brief       SDIO + SD 卡底层驱动（F407 版，使用 HAL SD 库）
 * @note        PC8-D0, PC9-D1, PC10-D2, PC11-D3, PC12-SCK, PD2-CMD
 *              1-bit 24MHz, DMA2 Stream3(RX)/Stream6(TX) Channel4
 ****************************************************************************************************
 */

#include "bsp_sdio_sd.h"
#include "bsp_usart.h"
#include <stdint.h>
#include <string.h>

/* USART1 实例（供 debug 打印使用） */
extern struct bsp_usart s_usart1;

/* ============================================================================
 * 全局句柄和卡信息（供 diskio.c 等模块引用）
 * ============================================================================ */
SD_HandleTypeDef       g_sd_handle;
HAL_SD_CardInfoTypeDef g_sd_card_info;

/* DMA 句柄（extern 供 stm32f4xx_it.c 使用） */
DMA_HandleTypeDef g_sd_hdma_rx;
DMA_HandleTypeDef g_sd_hdma_tx;

/* DMA 传输完成标志（HAL_SD_RxCpltCallback / TxCpltCallback 中置位） */
static volatile uint8_t g_sd_xfer_done = 0;

/* DMA 诊断用 bounce buffer（32 位对齐） */
static uint8_t g_sd_bounce[512] __attribute__((aligned(4)));

/* 诊断：FIFO 快照标志（仅记录一次） */
static volatile uint8_t g_sd_fifo_diagnosed = 0;

/* SDIO STA 寄存器：F4 上 DBCKEND=bit10, RXACT=bit13, RXFIFOF=bit17 */
/* 宏定义已在 stm32f4xx_ll_sdmmc.h / stm32f407xx.h 中提供，这里无需额外定义 */

/* ============================================================================
 * HAL_SD_MspInit — HAL SD 低层初始化（GPIO + DMA + NVIC）
 *
 * HAL_SD_Init 内部自动回调此函数。
 * ============================================================================ */
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef gpio_init;

    UNUSED(hsd);

    /* ---- 1. 使能时钟 ---- */
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* ---- 2. SDIO GPIO 引脚 ---- */

    /* SDIO 数据脚 D0-D3：PC8-D0, PC9-D1, PC10-D2, PC11-D3 */
    gpio_init.Pin       = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    gpio_init.Mode      = GPIO_MODE_AF_PP;
    gpio_init.Pull      = GPIO_PULLUP;
    gpio_init.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    /* SDIO 时钟脚：PC12-CK，不允许上拉 */
    gpio_init.Pin       = GPIO_PIN_12;
    gpio_init.Pull      = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio_init);

    /* SDIO 命令脚：PD2-CMD */
    gpio_init.Pin       = GPIO_PIN_2;
    gpio_init.Pull      = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOD, &gpio_init);

    /* ---- 3. DMA2 初始化（SDIO RX: Stream3, TX: Stream6, 均为 Channel4） ---- */

    /* DMA2 Stream3: SDIO RX (和 SD 工程完全对齐) */
    g_sd_hdma_rx.Instance                 = DMA2_Stream3;
    g_sd_hdma_rx.Init.Channel             = DMA_CHANNEL_4;
    g_sd_hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    g_sd_hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    g_sd_hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    g_sd_hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    g_sd_hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    g_sd_hdma_rx.Init.Mode                = DMA_PFCTRL;
    g_sd_hdma_rx.Init.Priority            = DMA_PRIORITY_LOW;
    g_sd_hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
    g_sd_hdma_rx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    g_sd_hdma_rx.Init.MemBurst            = DMA_MBURST_INC4;
    g_sd_hdma_rx.Init.PeriphBurst         = DMA_PBURST_INC4;
    HAL_DMA_Init(&g_sd_hdma_rx);
    __HAL_LINKDMA(hsd, hdmarx, g_sd_hdma_rx);

    /* DMA2 Stream6: SDIO TX (和 SD 工程完全对齐) */
    g_sd_hdma_tx.Instance                 = DMA2_Stream6;
    g_sd_hdma_tx.Init.Channel             = DMA_CHANNEL_4;
    g_sd_hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    g_sd_hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    g_sd_hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;
    g_sd_hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    g_sd_hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    g_sd_hdma_tx.Init.Mode                = DMA_PFCTRL;
    g_sd_hdma_tx.Init.Priority            = DMA_PRIORITY_LOW;
    g_sd_hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
    g_sd_hdma_tx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
    g_sd_hdma_tx.Init.MemBurst            = DMA_MBURST_INC4;
    g_sd_hdma_tx.Init.PeriphBurst         = DMA_PBURST_INC4;
    HAL_DMA_Init(&g_sd_hdma_tx);
    __HAL_LINKDMA(hsd, hdmatx, g_sd_hdma_tx);

    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

    HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);

    /* ---- 4. NVIC（SDIO 中断，优先级必须高于 DMA） ---- */
    HAL_NVIC_SetPriority(SDIO_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
}
/* ============================================================================
 * sd_init — 初始化 SD 卡（4-bit 24MHz DMA）
 *
 * 注意：初始化只做一次，第二次调用直接返回 0（跳过 HAL_SD_Init 重新卡识别）。
 * HAL_SD_Init 会发送 CMD0 复位卡，4-bit 模式会被破坏，后续再切 4-bit
 * 时序与第一次不一致导致失败。test_tx 也是仅初始化一次。
 * ============================================================================ */
uint8_t sd_init(void)
{
    static uint8_t sd_initialized = 0;

    if (sd_initialized)
        return 0;

    g_sd_handle.Instance                 = SDIO;
    g_sd_handle.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
    g_sd_handle.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    g_sd_handle.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    g_sd_handle.Init.BusWide             = SDIO_BUS_WIDE_1B;
    g_sd_handle.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    g_sd_handle.Init.ClockDiv            = 1;  /* 48MHz / (1+2) = 16MHz，与 test_tx 一致 */

    if (HAL_SD_Init(&g_sd_handle) != HAL_OK) {
        bsp_usart_printf(&s_usart1, "[SD_DBG] HAL_SD_Init FAILED (err=0x%08lX)\r\n", g_sd_handle.ErrorCode);
        return 1;
    }

    /* ── 使用 HAL 标准方式切换 4-bit 总线（与 SD 工程一致） ── */
    if (HAL_SD_ConfigWideBusOperation(&g_sd_handle, SDIO_BUS_WIDE_4B) != HAL_OK) {
        bsp_usart_printf(&s_usart1, "[SD_DBG] ConfigWideBus FAILED (err=0x%08lX), stay 1-bit\r\n",
                         g_sd_handle.ErrorCode);
    } else {
        bsp_usart_printf(&s_usart1, "[SD_DBG] 4-bit mode OK\r\n");
    }

    if (HAL_SD_GetCardInfo(&g_sd_handle, &g_sd_card_info) != HAL_OK) {
        bsp_usart_printf(&s_usart1, "[SD_DBG] HAL_SD_GetCardInfo FAILED\r\n");
        return 1;
    }

    /* HAL_SD_Init 结束后，STA 寄存器中 CMDREND 还残留着初始化最后一条命令
     * （CMD16 SET_BLOCKLEN）的完成标志。不消除的话，后续第一次数据命令
     * （CMD17/CMD24/CMD18/CMD25）在 SDMMC_GetCmdResp1 中会看到旧的 CMDREND
     * 而提前退出轮询循环，此时 RESPCMD 尚未刷新为新命令索引，导致命令校验失败。
     * 参见 SDMMC_GetCmdResp1 的实现。 */
    __HAL_SD_CLEAR_FLAG(&g_sd_handle, SDIO_STATIC_FLAGS);

    bsp_usart_printf(&s_usart1, "[SD_DBG] sd_init OK: type=%lu blk_nbr=%lu\r\n",
                     g_sd_card_info.CardType, g_sd_card_info.LogBlockNbr);
    sd_initialized = 1;
    return 0;
}


/* ============================================================================
 * sd_read_disk — 读扇区（DMA 模式）
 *
 * 4-bit + DMA_PFCTRL + FIFO_THRESHOLD_FULL + BURST_INC4 下 DMA 已验证稳定。
 * 通过回调 HAL_SD_RxCpltCallback 设置 g_sd_xfer_done 标志来判断完成。
 * ============================================================================ */
uint8_t sd_read_disk(uint8_t *buf, uint32_t sector, uint32_t cnt)
{
    uint32_t timeout;

    if ((cnt == 0) || (buf == NULL))
        return 1;

    g_sd_xfer_done = 0;
    SDIO->ICR = 0xFFFFFFFF;

    if (HAL_SD_ReadBlocks_DMA(&g_sd_handle, buf, sector, cnt) != HAL_OK)
        return 1;

    timeout = HAL_GetTick();
    while (g_sd_xfer_done == 0)
    {
        if ((HAL_GetTick() - timeout) > 5000UL)
            return 1;
    }

    return 0;
}


/* ============================================================================
 * sd_write_disk — 写扇区（CPU 轮询模式，不用 DMA）
 *
 * 使用 HAL_SD_WriteBlocks（轮询模式），写完后轮询 HAL_SD_GetCardState
 * 等待卡从 Programming 状态恢复到 Transfer 状态。
 * ============================================================================ */
uint8_t sd_write_disk(uint8_t *buf, uint32_t sector, uint32_t cnt)
{
    HAL_SD_CardStateTypeDef card_state;

    if ((cnt == 0) || (buf == NULL))
        return 1;

    /* 清除残留标志（CMDREND 等），防止 HAL 内部命令响应检测读到旧值 */
    SDIO->ICR = 0xFFFFFFFF;

    if (HAL_SD_WriteBlocks(&g_sd_handle, buf, sector, cnt, 5000UL) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[SD_DBG] WriteBlocks FAIL: State=%ld Err=0x%08lX Ctx=0x%08lX\r\n",
                         (long)g_sd_handle.State, (unsigned long)g_sd_handle.ErrorCode,
                         (unsigned long)g_sd_handle.Context);
        return 1;
    }

    /* 写操作后卡进入 Programming 状态（DAT0 拉低），轮询等待卡就绪 */
    {
        uint32_t wait = HAL_GetTick();
        do
        {
            /* 清 ICR，确保 CMD13（GetCardState）的 CMDREND 干净 */
            SDIO->ICR = 0xFFFFFFFF;
            card_state = HAL_SD_GetCardState(&g_sd_handle);
            if (HAL_GetTick() - wait > 5000UL)
            {
                bsp_usart_printf(&s_usart1, "[SD_DBG] WriteBlocks card busy timeout state=%d\r\n", (int)card_state);
                return 1;
            }
        } while (card_state != HAL_SD_CARD_TRANSFER);
    }
    return 0;
}


/* ============================================================================
 * HAL SD 回调函数（DMA 传输完成时由 HAL 内部调用）
 * ============================================================================ */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd->Instance == SDIO)
        g_sd_xfer_done = 1;
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd->Instance == SDIO)
        g_sd_xfer_done = 1;
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    /* 传输出错也标记完成，防止永久卡住 */
    if (hsd->Instance == SDIO)
        g_sd_xfer_done = 1;
}


/* ============================================================================
 * sd_dma_read_diag — DMA 单扇区读诊断（通用诊断函数）
 *
 * 以 DMA 模式读指定扇区，含 DBCKEND 检测 + CPU 排空 Fallback。
 * - sector: 安全扇区（>= 1000，避免破坏 FAT/MBR）
 * - return: 0=数据正确（DMA 完成或 Fallback 排空完整），1=失败
 * ============================================================================ */
static uint8_t sd_dma_read_diag(uint32_t sector, uint8_t *dmabuf, uint8_t trace)
{
    uint32_t ndtr_val, sta_val;
    int i;
    uint8_t drain_used = 0;

    /* 填充特征值 */
    for (i = 0; i < 512 / 4; i++)
        ((uint32_t *)dmabuf)[i] = 0xA5A5A5A5UL;

    SDIO->ICR = 0xFFFFFFFF;
    g_sd_xfer_done = 0;

    if (HAL_SD_ReadBlocks_DMA(&g_sd_handle, dmabuf, sector, 1) != HAL_OK)
    {
        if (trace) bsp_usart_printf(&s_usart1, "[DMA_DBG] ReadBlocks_DMA FAILED\r\n");
        return 1;
    }

    {
        uint32_t wait = HAL_GetTick();
        while (g_sd_xfer_done == 0)
        {
            ndtr_val = g_sd_hdma_rx.Instance->NDTR;
            sta_val  = SDIO->STA;

            /* DBCKEND(bit10) 触发，DMA NDTR 未归零 → 开启 CPU 排空 Fallback */
            if ((sta_val & SDIO_STA_DBCKEND) && (ndtr_val > 0))
            {
                uint32_t done_words = 128UL - ndtr_val;
                uint8_t *p = dmabuf + done_words * 4;
                uint32_t words_read = 0;
                uint32_t poll;

                if (trace)
                {
                    bsp_usart_printf(&s_usart1,
                        "[DMA_DBG] DBCKEND start: NDTR=%lu FIFOCNT=%lu done=%lu/128\r\n",
                        (unsigned long)ndtr_val,
                        (unsigned long)(SDIO->FIFOCNT & 0x7FU),
                        (unsigned long)done_words);
                }

                /* 1. 中止 DMA 流（HAL_DMA_Abort 负责清 EN 位 + 清全部中断标志） */
                HAL_DMA_Abort(&g_sd_hdma_rx);

                /* 2. 关 SDIO DMAEN，让 DPSM 切换到 CPU 模式 */
                SDIO->DCTRL &= ~SDIO_DCTRL_DMAEN;

                /* 3. 触发 DTEN 重装载：清除再置位，让 DPSM 重新评估状态并推出内部缓冲 */
                SDIO->DCTRL &= ~((uint32_t)SDIO_DCTRL_DTEN);
                for (volatile int d = 0; d < 10; d++) ;
                SDIO->DCTRL |= SDIO_DCTRL_DTEN;

                /* 4. 短延迟，等 DPSM 将残留数据推入 FIFO */
                for (volatile int d = 0; d < 20; d++) ;

                /* 5. CPU 轮询 RXDAVL 读残留数据 */
                poll = HAL_GetTick();
                while (words_read < ndtr_val)
                {
                    if (SDIO->STA & SDIO_STA_RXDAVL)
                    {
                        uint32_t d = SDIO->FIFO;
                        *p++ = (uint8_t)(d);
                        *p++ = (uint8_t)(d >> 8);
                        *p++ = (uint8_t)(d >> 16);
                        *p++ = (uint8_t)(d >> 24);
                        words_read++;
                    }
                    if (SDIO->STA & SDIO_STA_DATAEND)
                        break;
                    if ((HAL_GetTick() - poll) > 100UL)
                        break;
                }

                /* 6. 等 DATAEND（DPSM 进入 IDLE） */
                poll = HAL_GetTick();
                while (!(SDIO->STA & SDIO_STA_DATAEND))
                {
                    /* 继续尝试读残留 */
                    if (SDIO->STA & SDIO_STA_RXDAVL)
                    {
                        uint32_t d = SDIO->FIFO;
                        *p++ = (uint8_t)(d);
                        *p++ = (uint8_t)(d >> 8);
                        *p++ = (uint8_t)(d >> 16);
                        *p++ = (uint8_t)(d >> 24);
                        words_read++;
                    }
                    if ((HAL_GetTick() - poll) > 1000UL)
                        break;
                }

                drain_used = 1;
                if (trace)
                {
                    bsp_usart_printf(&s_usart1,
                        "[DMA_DBG] DBCKEND done: words_read=%lu/4 drain=%d STA=0x%08lX\r\n",
                        (unsigned long)words_read, (int)drain_used, (unsigned long)SDIO->STA);
                }
                break;
            }

            if ((HAL_GetTick() - wait) > 5000UL)
            {
                if (trace)
                {
                    bsp_usart_printf(&s_usart1,
                        "[DMA_DBG] TIMEOUT NDTR=%lu STA=0x%08lX FIFOCNT=%lu\r\n",
                        (unsigned long)ndtr_val, (unsigned long)sta_val,
                        (unsigned long)(SDIO->FIFOCNT & 0x7FU));
                }
                break;
            }
        }
    }

    /* 清理 HAL 句柄 */
    __HAL_SD_DISABLE_IT(&g_sd_handle, SDIO_IT_DATAEND | SDIO_IT_DCRCFAIL
                        | SDIO_IT_DTIMEOUT | SDIO_IT_RXOVERR);
    __HAL_SD_CLEAR_FLAG(&g_sd_handle, SDIO_STATIC_FLAGS);
    g_sd_handle.State = HAL_SD_STATE_READY;
    g_sd_handle.Context = SD_CONTEXT_NONE;

    /* 检查是否有非特征值的数据写入 */
    {
        int has_data = 0;
        for (i = 0; i < 512 / 4; i++)
        {
            if (((uint32_t *)dmabuf)[i] != 0xA5A5A5A5UL)
            { has_data = 1; break; }
        }
        return (has_data && (g_sd_xfer_done || drain_used)) ? 0 : 1;
    }
}


/* ============================================================================
 * sd_dma_full_test — DMA 对比诊断：PIO 写 + DMA 读 + DBCKEND 排空 + PIO 对比
 *
 * 使用安全扇区（远离 FAT 区域），不破坏文件系统。
 * ============================================================================ */
uint8_t sd_dma_full_test(uint32_t sector)
{
    uint8_t dmabuf[512] __attribute__((aligned(4)));
    uint8_t piobuf[512] __attribute__((aligned(4)));
    int i;

    bsp_usart_printf(&s_usart1, "\r\n[DMA_DBG] ===== FULL DMA DIAG (sector %lu) =====\r\n",
                     (unsigned long)sector);

    /* ---- Phase 1: PIO 写入特征数据 ---- */
    for (i = 0; i < 512 / 4; i++)
        ((uint32_t *)g_sd_bounce)[i] = 0xD0A0D0A0UL + (uint32_t)i;

    SDIO->ICR = 0xFFFFFFFF;
    if (HAL_SD_WriteBlocks(&g_sd_handle, g_sd_bounce, sector, 1, 5000UL) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] PIO Write FAILED\r\n");
        return 1;
    }
    {
        HAL_SD_CardStateTypeDef cs;
        uint32_t wait = HAL_GetTick();
        do {
            SDIO->ICR = 0xFFFFFFFF;
            cs = HAL_SD_GetCardState(&g_sd_handle);
        } while (cs != HAL_SD_CARD_TRANSFER && (HAL_GetTick() - wait) < 5000UL);
    }
    bsp_usart_printf(&s_usart1, "[DMA_DBG] PIO Write OK\r\n");

    /* ---- Phase 2: DMA 读回 + DBCKEND Fallback ---- */
    if (sd_dma_read_diag(sector, dmabuf, 1) != 0)
    {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] sd_dma_read_diag FAILED\r\n");
        /* 继续做 PIO 对比看看数据是否在卡上 */
    }

    /* ---- Phase 3: PIO 读回对比 ---- */
    SDIO->ICR = 0xFFFFFFFF;
    if (HAL_SD_ReadBlocks(&g_sd_handle, piobuf, sector, 1, 5000UL) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] PIO readback FAILED\r\n");
        return 1;
    }

    /* ---- 三路对比 ---- */
    int dmamatch = (memcmp(dmabuf, g_sd_bounce, 512) == 0);
    int piomatch = (memcmp(piobuf, g_sd_bounce, 512) == 0);
    int dmapiomatch = (memcmp(dmabuf, piobuf, 512) == 0);

    bsp_usart_printf(&s_usart1, "[DMA_DBG] DMA_vs_write=%s PIO_vs_write=%s DMA_vs_PIO=%s\r\n",
                     dmamatch ? "OK" : "MISMATCH",
                     piomatch ? "OK" : "MISMATCH",
                     dmapiomatch ? "OK" : "MISMATCH");

    if (dmapiomatch) {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] first_8_words (same): ");
        for (i = 0; i < 8; i++)
            bsp_usart_printf(&s_usart1, "%08lX ", ((uint32_t *)dmabuf)[i]);
        bsp_usart_printf(&s_usart1, "\r\n");
    } else {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] DMA_first_8: ");
        for (i = 0; i < 8; i++) bsp_usart_printf(&s_usart1, "%08lX ", ((uint32_t *)dmabuf)[i]);
        bsp_usart_printf(&s_usart1, "\r\n");
        bsp_usart_printf(&s_usart1, "[DMA_DBG] PIO_first_8:  ");
        for (i = 0; i < 8; i++) bsp_usart_printf(&s_usart1, "%08lX ", ((uint32_t *)piobuf)[i]);
        bsp_usart_printf(&s_usart1, "\r\n");
    }

    return (dmapiomatch ? 0 : 1);
}


/* ============================================================================
 * sd_dma_read_single_test — DMA 单扇区读诊断
 * ============================================================================ */
uint8_t sd_dma_read_single_test(uint32_t sector)
{
    uint8_t dmabuf[512] __attribute__((aligned(4)));
    uint8_t piobuf[512] __attribute__((aligned(4)));
    int i;
    int dma_ok, cmp;

    bsp_usart_printf(&s_usart1, "\r\n[DMA_DBG] ===== DMA READ DIAG (sector %lu) =====\r\n",
                     (unsigned long)sector);

    /* ---- Phase 1: PIO 写基准数据 ---- */
    for (i = 0; i < 512 / 4; i++)
        ((uint32_t *)g_sd_bounce)[i] = 0xD0A0D0A0UL + (uint32_t)i;

    SDIO->ICR = 0xFFFFFFFF;
    if (HAL_SD_WriteBlocks(&g_sd_handle, g_sd_bounce, sector, 1, 5000UL) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] PIO Write FAILED\r\n");
        return 1;
    }
    {
        HAL_SD_CardStateTypeDef cs;
        uint32_t wait = HAL_GetTick();
        do {
            SDIO->ICR = 0xFFFFFFFF;
            cs = HAL_SD_GetCardState(&g_sd_handle);
        } while (cs != HAL_SD_CARD_TRANSFER && (HAL_GetTick() - wait) < 5000UL);
    }

    dma_ok = sd_dma_read_diag(sector, dmabuf, 1);

    /* ---- Phase 2: PIO 读同一扇区 ---- */
    SDIO->ICR = 0xFFFFFFFF;
    if (HAL_SD_ReadBlocks(&g_sd_handle, piobuf, sector, 1, 5000UL) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] PIO read FAILED\r\n");
        return 1;
    }

    cmp = (memcmp(dmabuf, piobuf, 512) == 0);
    bsp_usart_printf(&s_usart1, "[DMA_DBG] DMA_vs_PIO=%s\r\n", cmp ? "MATCH" : "MISMATCH");

    if (!cmp)
    {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] DMA_first_8: ");
        for (i = 0; i < 8; i++) bsp_usart_printf(&s_usart1, "%08lX ", ((uint32_t *)dmabuf)[i]);
        bsp_usart_printf(&s_usart1, "\r\n[DMA_DBG] PIO_first_8:  ");
        for (i = 0; i < 8; i++) bsp_usart_printf(&s_usart1, "%08lX ", ((uint32_t *)piobuf)[i]);
        bsp_usart_printf(&s_usart1, "\r\n");
    }

    return (cmp && !dma_ok) ? 0 : 1;
}


/* ============================================================================
 * sd_dma_write_single_test — DMA 单扇区写诊断
 * ============================================================================ */
uint8_t sd_dma_write_single_test(uint32_t sector)
{
    int i;

    SDIO->ICR = 0xFFFFFFFF;
    for (i = 0; i < 512 / 4; i++)
        ((uint32_t *)g_sd_bounce)[i] = 0xDEADBEEFUL;
    ((uint32_t *)g_sd_bounce)[0] = 0xD0A0D0A0UL;

    g_sd_xfer_done = 0;
    if (HAL_SD_WriteBlocks_DMA(&g_sd_handle, g_sd_bounce, sector, 1) != HAL_OK)
    {
        bsp_usart_printf(&s_usart1, "[DMA_DBG] HAL_SD_WriteBlocks_DMA FAILED\r\n");
        return 1;
    }

    {
        uint32_t wait = HAL_GetTick();
        while (g_sd_xfer_done == 0)
        {
            if ((HAL_GetTick() - wait) > 5000UL)
            {
                bsp_usart_printf(&s_usart1, "[DMA_DBG] WRITE_TIMEOUT NDTR=%lu\r\n",
                                 (unsigned long)g_sd_hdma_tx.Instance->NDTR);
                break;
            }
        }
    }

    if (g_sd_xfer_done == 0)
    {
        __HAL_SD_DISABLE_IT(&g_sd_handle, SDIO_IT_DATAEND | SDIO_IT_DCRCFAIL
                            | SDIO_IT_DTIMEOUT | SDIO_IT_TXUNDERR);
        __HAL_SD_CLEAR_FLAG(&g_sd_handle, SDIO_STATIC_FLAGS);
        g_sd_handle.State = HAL_SD_STATE_READY;
        g_sd_handle.Context = SD_CONTEXT_NONE;
        bsp_usart_printf(&s_usart1, "[DMA_DBG] DMA Write FAILED (xfer_done=0)\r\n");
        return 1;
    }

    /* 等卡就绪 */
    {
        HAL_SD_CardStateTypeDef cs;
        uint32_t wait = HAL_GetTick();
        do {
            SDIO->ICR = 0xFFFFFFFF;
            cs = HAL_SD_GetCardState(&g_sd_handle);
        } while (cs != HAL_SD_CARD_TRANSFER && (HAL_GetTick() - wait) < 5000UL);
    }

    bsp_usart_printf(&s_usart1, "[DMA_DBG] DMA_WRITE OK sector=%lu\r\n", (unsigned long)sector);
    return 0;
}


/* ============================================================================
 * sd_read_sector_raw — 纯寄存器级单扇区读（调试用，避开 HAL）
 *
 * 手动配置 SDIO 数据路径，发送 CMD17，PIO 轮询 RXDAVL 读 FIFO。
 * ============================================================================ */
uint8_t sd_read_sector_raw(uint8_t *buf, uint32_t sector)
{
    uint32_t data;
    int i, timeout, wordcount;
    uint8_t *p = buf;

    if (buf == NULL) return 1;

    /* 0. 彻底清除残留标志 */
    SDIO->ICR = 0xFFFFFFFF;
    SDIO->DCTRL = 0;

    /* 1. 数据路径配置 */
    SDIO->DTIMER = 0xFFFFFFU;                  /* 超时 */
    SDIO->DLEN   = 512;                        /* 1 块 = 512 字节 */
    SDIO->DCTRL  = (9 << 4) | (1 << 1) | (0 << 2) | 1;  /* DBLOCKSIZE=9(512B), DTDIR=1(读), DTMODE=0(块), DTEN=1 */

    /* 2. 发送 CMD17 */
    SDIO->ARG  = sector;
    SDIO->CMD  = (17 << 24) | (1 << 10) | (1 << 12) | 1;  /* CMD17, CPSM=1, WaitResp=Short */

    /* 3. 等 CMDREND */
    timeout = 1000000;
    while (!(SDIO->STA & (1U << 6)))           /* CMDREND flag */
        if (--timeout == 0) return 1;

    /* 4. 清除命令标志 */
    SDIO->ICR = 0xFFFFFFFF;

    /* 5. PIO 读 FIFO — 轮询 RXDAVL（FIFO 非空），逐 word 读取 */
    wordcount = 0;
    for (i = 0; i < 128; i++)                  /* 512 字节 = 128 words */
    {
        timeout = 1000000;
        while (!(SDIO->STA & (1U << 9)))       /* RXDAVL: FIFO 至少 1 word */
        {
            if (SDIO->STA & (1U << 8))         /* DATAEND */
                break;
            if (--timeout == 0) return 2;
        }

        data = SDIO->FIFO;
        *p++ = (uint8_t)(data);
        *p++ = (uint8_t)(data >> 8);
        *p++ = (uint8_t)(data >> 16);
        *p++ = (uint8_t)(data >> 24);
        wordcount++;
    }

    /* 6. 等 DATAEND（已读满 128 word，等 DPSM 完成） */
    timeout = 1000000;
    while (!(SDIO->STA & (1U << 8)))
        if (--timeout == 0) return 3;

    /* 7. 清理 */
    SDIO->ICR = 0xFFFFFFFF;
    return 0;
}
