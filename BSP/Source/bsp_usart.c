/**
 * @file    bsp_usart.c
 * @brief   USART 驱动实现（F407 版，适配 F4 寄存器差异）
 *
 * ============================================================================
 *  架构说明
 * ============================================================================
 *
 *  发送路径（两种）：
 *
 *    send_poll  → 直接操作 DR/SR 寄存器，阻塞直到发完
 *                 适用于少量数据或调试信息
 *
 *    send_dma   → 数据 → tx_buf → DMA Stream → USART DR
 *                 非阻塞，发送完成由 proc 中的 tx_inquire 轮询检查
 *                 DMA 发送期间不能再发新数据（dmaing 标志保护）
 *
 *  接收路径（DMA + IDLE 中断定帧）：
 *
 *    DMA Stream 一直把 USART DR 的数据搬到 rx_buf（循环模式）
 *    发完一帧 → 总线空闲 → USART 硬件置 IDLE 标志 → 进 irq_handler
 *    irq_handler:
 *      1. 读 DMA NDTR 计算当前位置，对比上次位置算出帧长
 *      2. 把帧拷贝到 rx_fifo（FIFO 环形队列）
 *    主循环：
 *      is_rx_complete → 有帧 → recv_frame → 从 FIFO 弹出 → 处理数据
 *
 *    FIFO 作用：解耦中断和主循环，收发速率不匹配时 FIFO 缓存。
 *
 * ============================================================================
 *  F4 vs H7 寄存器差异
 * ============================================================================
 *
 *  H7              F4              说明
 *  ─────────────   ─────────────   ───────────────────────
 *  ISR             SR              状态寄存器
 *  RDR             DR              接收数据寄存器
 *  TDR             DR              发送数据寄存器
 *  ICR             无              中断清除（F4 靠读 SR 后读/写 DR 清标志）
 *  USART_ISR_TXE   USART_SR_TXE    TX 空标志
 *  USART_ISR_TC    USART_SR_TC     发送完成标志
 *  USART_ISR_RXNE  USART_SR_RXNE   RX 非空标志
 *  USART_ISR_IDLE  USART_SR_IDLE   总线空闲标志
 *  USART_ISR_ORE   USART_SR_ORE    溢出错误标志
 *
 *  F4 TC 清除序列：读 SR（检测 TC=1）→ 写 DR（完成清除）
 *  F4 IDLE 清除：读 SR → 读 DR
 *  F4 ORE 清除：读 SR → 读 DR
 *
 * ============================================================================
 *  F4 DMA 差异
 * ============================================================================
 *
 *  - 无 DMAMUX：通过 DMA_SxCR.CHSEL[2:0] 选择通道
 *  - F407 所有 USART 固定使用 CH4（CHSEL=4=100b → CHSEL_2）
 *  - 无 TRBUFF 位（H7 Errata 2.22 工作区）
 *  - 无 D-Cache：无需 Clean/Invalidate 操作
 *
 * ============================================================================
 *  ORE 溢出处理
 * ============================================================================
 *
 *  ORE（OverRun Error）发生在 USART 接收速率高于 DMA 搬走数据的速率时。
 *  表现：USART SR 的 ORE 标志被置位，后续数据丢弃。
 *
 *  处理策略：检测到 ORE → 读 SR → 读 DR 清标志 → 重启 DMA
 *  如果频繁出现 ORE，说明波特率太高或 DMA 优先级不够。
 *
 * ============================================================================
 *  FIFO 环形队列逻辑
 * ============================================================================
 *
 *  head: 写入位置（irq_handler 用）
 *  tail: 读取位置（主循环用）
 *  count: 队列中帧数
 *
 *  写入：slots[head] = 数据  →  head++  →  count++
 *  读取：数据 = slots[tail]  →  tail++  →  count--
 *
 *  队列满（count == DEPTH）时新帧丢弃，不覆盖旧帧。
 *  读取时关中断保护 count/tail 一致性（写入在中断中，读取在主循环）。
 */

#include "bsp_usart.h"
#include <string.h>
#include <stdio.h>


/* ============================================================================
 * 内部辅助函数
 * ============================================================================ */

/**
 * @brief  初始化 FIFO 队列
 */
static void _fifo_init(struct bsp_usart_fifo *fifo)
{
    fifo->head  = 0;
    fifo->tail  = 0;
    fifo->count = 0;
}

/**
 * @brief  从 FIFO 弹出队首帧
 *
 * @param fifo  FIFO 指针
 * @param data  输出缓冲区
 * @param len   传入缓冲区大小，传出实际拷贝字节数
 * @return 1  成功
 * @return 0  队列空
 *
 * @note 读取过程中关中断保护 tail/count（因为写入在中断中）
 */
static uint8_t _fifo_pop(struct bsp_usart_fifo *fifo,
                         uint8_t *data, uint16_t *len)
{
    uint8_t  slot_idx;
    uint16_t slot_len;

    /* 队列空 */
    if (fifo->count == 0)
        return 0;

    slot_idx = fifo->tail;
    slot_len = fifo->slots[slot_idx].len;

    /* 如果用户缓冲区小于帧长，截断 */
    if (slot_len > *len)
        slot_len = *len;

    memcpy(data, fifo->slots[slot_idx].data, slot_len);

    /* 更新 tail/count，关中断防止写了一半被中断打断 */
    __disable_irq();
    fifo->tail = (slot_idx + 1) % fifo->depth;
    fifo->count--;
    __enable_irq();

    *len = slot_len;
    return 1;
}

/**
 * @brief  使能 GPIO 端口时钟
 *
 * @param port  GPIO 组（只支持 A/B/C/D）
 * @return 0  成功
 * @return -1 不支持的端口
 */
static int _gpio_clk_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA)      __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else return -1;
    return 0;
}

/**
 * @brief  根据 USART 实例获取 GPIO 复用功能号
 *
 * F407 映射：
 *   USART1/2/3/6  → AF7
 *   UART4/5/7/8   → AF8
 *
 * @param instance  USART 外设基址
 * @return GPIO_AF 编号
 */
static uint8_t _get_usart_af(USART_TypeDef *instance)
{
    if (instance == USART1 || instance == USART2 || instance == USART3)
        return GPIO_AF7_USART1;  /* AF7 */
    else
        return GPIO_AF8_UART4;   /* AF8 */
}

/**
 * @brief  清除 F4 USART 的 IDLE 标志
 *
 * F4 清除序列：读 SR → 读 DR
 */
static void _clear_idle_flag(struct bsp_usart *me)
{
    volatile uint32_t sr = READ_REG(me->instance->SR);
    volatile uint32_t dr = READ_REG(me->instance->DR);
    (void)sr;
    (void)dr;
}

/**
 * @brief  清除 F4 USART 的 ORE 标志
 *
 * F4 清除序列：读 SR → 读 DR
 */
static void _clear_ore_flag(struct bsp_usart *me)
{
    volatile uint32_t sr = READ_REG(me->instance->SR);
    volatile uint32_t dr = READ_REG(me->instance->DR);
    (void)sr;
    (void)dr;
}


/* ============================================================================
 * 初始化 / 反初始化
 * ============================================================================ */

int bsp_usart_init(struct bsp_usart *me, USART_TypeDef *instance,
                   GPIO_TypeDef *tx_port, uint16_t tx_pin,
                   GPIO_TypeDef *rx_port, uint16_t rx_pin,
                   uint32_t baudrate,
                   DMA_Stream_TypeDef *dma_tx, DMA_Stream_TypeDef *dma_rx,
                   uint8_t fifo_depth, uint16_t fifo_frame_max,
                   void *fifo_pool)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t i;

    /* ---------- 参数校验 ---------- */
    if (!me || !instance || !tx_port || !rx_port)
        return -1;

    /* ---------- 配置 FIFO ---------- */
    if (fifo_pool)
    {
        /* 外部内存池模式：用户提供内存 */
        uint32_t off = (uint32_t)fifo_depth * sizeof(struct bsp_usart_slot);
        me->rx_fifo.slots     = (struct bsp_usart_slot *)fifo_pool;
        me->rx_fifo.depth     = fifo_depth;
        me->rx_fifo.frame_max = fifo_frame_max;
        for (i = 0; i < fifo_depth; i++)
            me->rx_fifo.slots[i].data =
                (uint8_t *)fifo_pool + off + (uint32_t)i * fifo_frame_max;
    }
    else
    {
        /* 板载模式：使用内嵌 _def_slots / _def_data */
        if (fifo_depth     > BSP_USART_FIFO_DEF_DEPTH ||
            fifo_frame_max > BSP_USART_FIFO_DEF_FRAME)
            return -5;  /* 超出板载容量，需外部内存池 */

        if (fifo_depth == 0)
            fifo_depth = BSP_USART_FIFO_DEF_DEPTH;
        if (fifo_frame_max == 0)
            fifo_frame_max = BSP_USART_FIFO_DEF_FRAME;

        me->rx_fifo.slots     = me->_def_slots;
        me->rx_fifo.depth     = fifo_depth;
        me->rx_fifo.frame_max = fifo_frame_max;
        for (i = 0; i < fifo_depth; i++)
            me->rx_fifo.slots[i].data = me->_def_data[i];
    }
    _fifo_init(&me->rx_fifo);

    /* ---------- 保存配置 ---------- */
    me->instance = instance;
    me->tx_port  = tx_port;
    me->tx_pin   = tx_pin;
    me->rx_port  = rx_port;
    me->rx_pin   = rx_pin;
    me->baudrate = baudrate;
    me->dma_tx   = dma_tx;
    me->dma_rx   = dma_rx;

    /* ---------- 清零所有状态变量 ---------- */
    me->dmaing   = 0;
    me->tx_type  = 0;
    me->tx_sum   = 0;
    me->tx_cnt   = 0;
    me->rx_cnt   = 0;
    me->rx_sum   = 0;
    me->last_rx_pos = 0;
    me->rx_complete = 0;
    me->rx_timestamp = 0;

    memset(me->rx_buf, 0, BSP_USART_FRAME_SIZE);
    memset(me->tx_buf, 0, BSP_USART_FRAME_SIZE);

    /* ---------- 1. 配置 GPIO 为复用推挽 ---------- */
    if (_gpio_clk_enable(tx_port) != 0 || _gpio_clk_enable(rx_port) != 0)
        return -2;  /* 不支持的 GPIO 端口 */

    gpio.Pin       = tx_pin | rx_pin;
    gpio.Mode      = GPIO_MODE_AF_PP;        /* 复用推挽输出 */
    gpio.Pull      = GPIO_PULLUP;             /* 默认上拉 */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = _get_usart_af(instance);
    HAL_GPIO_Init(tx_port, &gpio);

    /* ---------- 2. 使能 USART 时钟 ---------- */
    if (instance == USART1)      __HAL_RCC_USART1_CLK_ENABLE();
    else if (instance == USART2) __HAL_RCC_USART2_CLK_ENABLE();
    else if (instance == USART3) __HAL_RCC_USART3_CLK_ENABLE();
    else if (instance == UART4)  __HAL_RCC_UART4_CLK_ENABLE();
    else return -3;  /* 不支持的 USART 实例 */

    /* ---------- 3. HAL 初始化（8N1 + 波特率） ---------- */
    {
        /*
         * 使用 static UART_HandleTypeDef 是因为 HAL_UART_Init 会保存指针到内部链表。
         * 如果栈变量函数退出后就被销毁，HAL 会访问野指针。
         * 多个 USART 实例共享一个 huart 没问题，因为每次 init 只改 Instance 字段，
         * 同一时间只配置一个实例。
         */
        static UART_HandleTypeDef huart;

        huart.Instance          = instance;
        huart.Init.BaudRate     = baudrate;
        huart.Init.WordLength   = UART_WORDLENGTH_8B;
        huart.Init.StopBits     = UART_STOPBITS_1;
        huart.Init.Parity       = UART_PARITY_NONE;
        huart.Init.Mode         = UART_MODE_TX_RX;
        huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
        huart.Init.OverSampling = UART_OVERSAMPLING_16;

        if (HAL_UART_Init(&huart) != HAL_OK)
            return -4;
    }

    /* ---------- 4. 配置 TX DMA ---------- */
    if (me->dma_tx)
    {
        /* 4a. 使能 DMA 时钟 */
        if (me->dma_tx >= DMA1_Stream0 && me->dma_tx <= DMA1_Stream7)
            __HAL_RCC_DMA1_CLK_ENABLE();
        else
            __HAL_RCC_DMA2_CLK_ENABLE();

        /* 4b. 停止 DMA 流 */
        CLEAR_BIT(me->dma_tx->CR, DMA_SxCR_EN);

        /* 4c. 清除 DMA 中断标志 */
        {
            uint32_t stream_idx = ((uint32_t)me->dma_tx - (uint32_t)DMA1_Stream0) /
                                  ((uint32_t)DMA1_Stream1 - (uint32_t)DMA1_Stream0);
            if (stream_idx < 4)
                DMA1->LIFCR = 0x3FUL << (stream_idx * 6);
            else
                DMA1->HIFCR = 0x3FUL << ((stream_idx - 4) * 6);
        }

        /* 4d. 配置 TX DMA CR
         *   MINC   = 1   存储器地址递增
         *   DIR    = 01  存储器 → 外设
         *   CHSEL  = 4   CH4（所有 USART 固定）
         *   PSIZE  = 00  8 位
         *   MSIZE  = 00  8 位
         *   PL     = 00  最低优先级
         */
        me->dma_tx->CR = DMA_SxCR_MINC | DMA_SxCR_DIR_0 | BSP_USART_DMA_CHSEL;

        /* 4e. 外设地址 = USART 数据寄存器 */
        me->dma_tx->PAR = (uint32_t)&me->instance->DR;

        /* 4f. 存储器地址 = 发送缓冲区 */
        me->dma_tx->M0AR = (uint32_t)me->tx_buf;

        /* 4g. FCR = 直接模式 */
        me->dma_tx->FCR = 0;
    }

    /* ---------- 5. 使能 USART DMA 发送 ---------- */
    if (me->dma_tx)
    {
        SET_BIT(me->instance->CR3, USART_CR3_DMAT);
    }

    /* ---------- 6. 配置 RX DMA ---------- */
    if (me->dma_rx)
    {
        /* 6a. 使能 DMA 时钟 */
        if (me->dma_rx >= DMA1_Stream0 && me->dma_rx <= DMA1_Stream7)
            __HAL_RCC_DMA1_CLK_ENABLE();
        else
            __HAL_RCC_DMA2_CLK_ENABLE();

        /* 6b. 停止 DMA 流 */
        CLEAR_BIT(me->dma_rx->CR, DMA_SxCR_EN);

        /* 6c. 清除 DMA 中断标志 */
        {
            uint32_t stream_idx = ((uint32_t)me->dma_rx - (uint32_t)DMA1_Stream0) /
                                  ((uint32_t)DMA1_Stream1 - (uint32_t)DMA1_Stream0);
            if (stream_idx < 4)
                DMA1->LIFCR = 0x3FUL << (stream_idx * 6);
            else
                DMA1->HIFCR = 0x3FUL << ((stream_idx - 4) * 6);
        }

        /* 6d. 配置 RX DMA CR
         *   CIRC   = 1   循环模式
         *   MINC   = 1   存储器地址递增
         *   DIR    = 00  外设 → 存储器
         *   CHSEL  = 4   CH4（所有 USART 固定）
         *   PSIZE  = 00  8 位
         *   MSIZE  = 00  8 位
         *   PL     = 00  最低优先级
         */
        me->dma_rx->CR = DMA_SxCR_CIRC | DMA_SxCR_MINC | BSP_USART_DMA_CHSEL;

        /* 6e. 外设地址 = USART 数据寄存器 */
        me->dma_rx->PAR = (uint32_t)&me->instance->DR;

        /* 6f. 存储器地址 = 环形接收缓冲区 */
        me->dma_rx->M0AR = (uint32_t)me->rx_buf;

        /* 6g. NDTR = 缓冲区大小 */
        me->dma_rx->NDTR = BSP_USART_FRAME_SIZE;

        /* 6h. FCR = 直接模式 */
        me->dma_rx->FCR = 0;
    }

    /* ---------- 7. 使能 USART DMA 接收 ---------- */
    if (me->dma_rx)
    {
        SET_BIT(me->instance->CR3, USART_CR3_DMAR);
    }

    /* ---------- 8. 使能 IDLE 中断 ---------- */
    SET_BIT(me->instance->CR1, USART_CR1_IDLEIE);

    /* ---------- 9. 配置 NVIC 中断 ---------- */
    if (me->instance == USART1) {
        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    } else if (me->instance == USART2) {
        HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    } else if (me->instance == USART3) {
        HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
    } else if (me->instance == UART4) {
        HAL_NVIC_SetPriority(UART4_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(UART4_IRQn);
    }

    me->initialized = true;

    /* 启动 RX DMA */
    if (me->dma_rx)
    {
        bsp_usart_clr_rx(me);
    }

    return 0;
}

int bsp_usart_deinit(struct bsp_usart *me)
{
    if (!me || !me->initialized)
        return -1;

    /* 释放 GPIO 引脚 */
    HAL_GPIO_DeInit(me->tx_port, me->tx_pin);
    HAL_GPIO_DeInit(me->rx_port, me->rx_pin);

    /* 关闭 USART 时钟 */
    if (me->instance == USART1)      __HAL_RCC_USART1_CLK_DISABLE();
    else if (me->instance == USART2) __HAL_RCC_USART2_CLK_DISABLE();
    else if (me->instance == USART3) __HAL_RCC_USART3_CLK_DISABLE();
    else if (me->instance == UART4)  __HAL_RCC_UART4_CLK_DISABLE();

    me->initialized = false;
    return 0;
}


/* ============================================================================
 * 轮询收发
 * ============================================================================ */

int bsp_usart_send_poll(struct bsp_usart *me, const uint8_t *data, uint16_t len)
{
    if (!me || !me->initialized || !data)
        return -1;

    /* 等待 TX 寄存器空 → 写入 → 直到全部写完 → 等待发送完成 */
    for (uint16_t i = 0; i < len; i++)
    {
        while (!(me->instance->SR & USART_SR_TXE));
        me->instance->DR = data[i];
    }
    while (!(me->instance->SR & USART_SR_TC));  /* 等最后一个字节发完 */
    return 0;
}

int bsp_usart_recv_poll(struct bsp_usart *me, uint8_t *data,
                        uint16_t len, uint32_t timeout_ms)
{
    uint32_t tick;
    if (!me || !me->initialized || !data)
        return -1;

    tick = HAL_GetTick();
    for (uint16_t i = 0; i < len; i++)
    {
        /* 等 RXNE 标志，同时检查超时 */
        while (!(me->instance->SR & USART_SR_RXNE))
        {
            if (HAL_GetTick() - tick > timeout_ms)
                return -2;  /* 超时返回 */
        }
        data[i] = (uint8_t)(me->instance->DR);
    }
    return 0;
}


/* ============================================================================
 * DMA 发送
 *
 * 关键点：
 *   连续发送时需等待上次 DMA 完成（dmaing 标志）
 *   超时 10000 轮后强制复位（防止 DMA 异常卡死）
 *   F4 无 D-Cache，无需 Clean 操作
 * ============================================================================ */

void bsp_usart_send_dma(struct bsp_usart *me, uint8_t *data, uint16_t len)
{
    uint32_t timeout;

    if (!me || !me->initialized)
        return;

    /* 长度截断，防止溢出 tx_buf */
    if (len >= BSP_USART_FRAME_SIZE)
        len = BSP_USART_FRAME_SIZE;

    /* 等待上次 DMA 完成，超时则强制复位 */
    timeout = 10000;
    while (me->dmaing && --timeout);

    if (timeout == 0)
    {
        /* DMA 卡死：强制停止 */
        if (me->dma_tx)
            CLEAR_BIT(me->dma_tx->CR, DMA_SxCR_EN);
        me->dmaing = 0;
        me->tx_type = 0;
    }

    /* 拷贝数据到内部 tx_buf */
    if (data != me->tx_buf)
        memcpy(me->tx_buf, data, len);

    me->tx_sum  = len;
    me->tx_cnt  = 0;
    me->tx_type = 1;
    me->dmaing  = 1;

    /* 启动 DMA 传输
     *
     * F4 DMA 第二次启动缺陷：DMA 传输完成后 M0AR 自动递增到缓冲区末尾，
     * 但 NDTR=0 / EN=0。必须等待 EN 清零后重写 M0AR+NDTR 才能再次使能。
     * 参考 test_tx 的 InitUsart2_DMA 做法。
     */
    if (me->dma_tx)
    {
        /* 1. 停止 DMA */
        CLEAR_BIT(me->dma_tx->CR, DMA_SxCR_EN);

        /* 2. 等待 EN 确实清零 */
        timeout = 100000;
        while ((me->dma_tx->CR & DMA_SxCR_EN) && --timeout);

        /* 3. 清除 DMA 所有状态标志（TCIF/HTIF/TEIF/DMEIF/FEIF）
         *
         * 关键！F4 DMA 第二次启动时，若 TCIF 还挂着，硬件可能认为
         * 传输已完成导致刚使能就停 EN，NDTR 不递减。
         * 参考 test_tx SCIB_BufTx 在 Enable 前调 DMA_ClearFlag。
         */
        {
            uint32_t stream_idx = ((uint32_t)me->dma_tx - (uint32_t)DMA1_Stream0) /
                                  ((uint32_t)DMA1_Stream1 - (uint32_t)DMA1_Stream0);
            if (stream_idx < 4)
                DMA1->LIFCR = 0x3FUL << (stream_idx * 6);
            else
                DMA1->HIFCR = 0x3FUL << ((stream_idx - 4) * 6);
        }

        /* 4. 重写 NDTR + M0AR + PAR */
        WRITE_REG(me->dma_tx->NDTR, len);
        WRITE_REG(me->dma_tx->M0AR, (uint32_t)me->tx_buf);
        WRITE_REG(me->dma_tx->PAR, (uint32_t)&me->instance->DR);

        /* 5. 使能 DMA */
        SET_BIT(me->dma_tx->CR, DMA_SxCR_EN);
    }
}

uint8_t bsp_usart_is_tx_busy(struct bsp_usart *me)
{
    if (!me) return 0;
    return (me->dmaing || me->tx_cnt != 0) ? 1 : 0;
}

void bsp_usart_clr_tx(struct bsp_usart *me)
{
    if (!me || !me->initialized)
        return;
    if (me->dma_tx)
        CLEAR_BIT(me->dma_tx->CR, DMA_SxCR_EN);
    memset(me->tx_buf, 0, BSP_USART_FRAME_SIZE);
    me->tx_cnt  = 0;
    me->tx_sum  = 0;
    me->dmaing  = 0;
}

void bsp_usart_tx_inquire(struct bsp_usart *me)
{
    if (!me || !me->initialized)
        return;
    if (me->tx_type != 1)
        return;

    /*
     * 检查 DMA 发送完成条件：
     *   NDTR==0           → DMA 已搬完所有字节
     *   SR_TC             → USART 已发出最后一个字节
     *
     * F4 无 ICR 寄存器，TC 不清除（硬件自动维护，下次发送完成仍会被置位）。
     */
    if (me->dma_tx && (READ_REG(me->dma_tx->NDTR) == 0) &&
        (READ_REG(me->instance->SR) & USART_SR_TC))
    {
        /* 复位状态，停 DMA */
        me->tx_cnt  = 0;
        me->tx_sum  = 0;
        me->tx_type = 0;
        me->dmaing  = 0;
        CLEAR_BIT(me->dma_tx->CR, DMA_SxCR_EN);
    }
}


/* ============================================================================
 * 帧接收（IDLE 中断 + FIFO）
 * ============================================================================ */

uint16_t bsp_usart_recv_frame(struct bsp_usart *me, uint8_t *data, uint16_t size)
{
    uint16_t len = size;
    if (!me || !data)
        return 0;
    if (_fifo_pop(&me->rx_fifo, data, &len) == 0)
        return 0;
    return (len > size) ? size : len;
}

uint8_t bsp_usart_is_rx_complete(struct bsp_usart *me)
{
    if (!me) return 0;
    return (me->rx_fifo.count > 0) ? 1 : 0;
}

uint16_t bsp_usart_get_rx_len(struct bsp_usart *me)
{
    if (!me || me->rx_fifo.count == 0) return 0;
    return me->rx_fifo.slots[me->rx_fifo.tail].len;
}

void bsp_usart_clr_rx(struct bsp_usart *me)
{
    volatile uint32_t dummy;
    if (!me || !me->initialized)
        return;

    /* 停 DMA */
    if (me->dma_rx)
        CLEAR_BIT(me->dma_rx->CR, DMA_SxCR_EN);

    /* 读空 USART DR 寄存器，清掉残留数据 */
    while (READ_REG(me->instance->SR) & USART_SR_RXNE)
    {
        dummy = READ_REG(me->instance->DR);
        (void)dummy;
    }

    memset(me->rx_buf, 0, BSP_USART_FRAME_SIZE);

    /* 复位 NDTR，重启 DMA */
    if (me->dma_rx)
    {
        WRITE_REG(me->dma_rx->NDTR, BSP_USART_FRAME_SIZE);
        SET_BIT(me->dma_rx->CR, DMA_SxCR_EN);
    }

    me->rx_complete = 0;
    me->rx_cnt      = 0;
    me->last_rx_pos = 0;
    _fifo_init(&me->rx_fifo);
}

void bsp_usart_err_check(struct bsp_usart *me)
{
    if (!me || !me->initialized)
        return;

    /*
     * ORE（OverRun Error）处理：
     * 如果 USART 接收数据比 DMA 搬走快，硬件会置 ORE 标志。
     * 此时 USART 停止接收新数据。
     *
     * 恢复步骤：读 SR → 读 DR（清标志）→ 重启 DMA
     */
    if (READ_REG(me->instance->SR) & USART_SR_ORE)
    {
        _clear_ore_flag(me);

        if (me->dma_rx)
        {
            CLEAR_BIT(me->dma_rx->CR, DMA_SxCR_EN);
            WRITE_REG(me->dma_rx->NDTR, BSP_USART_FRAME_SIZE);
            me->last_rx_pos = 0;
            SET_BIT(me->dma_rx->CR, DMA_SxCR_EN);
        }
    }
}


/* ============================================================================
 * IDLE 中断处理
 *
 * 在 USART 总线空闲（即一帧发完）时触发。
 *
 * 处理步骤：
 *   1. 检查并清 IDLE 标志（F4：读 SR → 读 DR）
 *   2. 检查 ORE，有则恢复并直接返回
 *   3. 读 DMA NDTR 计算当前写入位置
 *   4. 当前 - 上次 = 本帧长度（考虑环形回绕）
 *   5. 把帧数据拷贝到 FIFO
 *   6. 更新 last_rx_pos
 *
 * 注意：本函数在中断上下文中执行，应尽量轻量。
 * ============================================================================ */

void bsp_usart_irq_handler(struct bsp_usart *me)
{
    uint16_t current_pos, frame_len, pos, i, copy_len;

    if (!me || !me->initialized)
        return;

    /* 只处理 IDLE 中断 */
    if (!(READ_REG(me->instance->SR) & USART_SR_IDLE))
        return;

    /* 清 IDLE 标志（F4：读 SR → 读 DR） */
    _clear_idle_flag(me);

    /* ---------- ORE 溢出恢复 ---------- */
    if (READ_REG(me->instance->SR) & USART_SR_ORE)
    {
        _clear_ore_flag(me);
        if (me->dma_rx)
        {
            CLEAR_BIT(me->dma_rx->CR, DMA_SxCR_EN);
            WRITE_REG(me->dma_rx->NDTR, BSP_USART_FRAME_SIZE);
            me->last_rx_pos = 0;
            SET_BIT(me->dma_rx->CR, DMA_SxCR_EN);
        }
        return;
    }

    /* 没有 DMA 接收就不能定帧 */
    if (!me->dma_rx)
        return;

    /* ---------- 计算帧长 ---------- */
    /*
     * DMA 循环模式：NDTR 从 FRAME_SIZE 递减到 0 后自动重载。
     * 当前已写入位置 = FRAME_SIZE - NDTR
     * 本帧长度 = 当前位置 - 上次位置
     * 如果回绕了（current < last），说明跨过了缓冲区末尾，
     * 帧长 = (FRAME_SIZE - last) + current
     */
    current_pos = BSP_USART_FRAME_SIZE - READ_REG(me->dma_rx->NDTR);
    if (current_pos >= me->last_rx_pos)
        frame_len = current_pos - me->last_rx_pos;
    else
        frame_len = (BSP_USART_FRAME_SIZE - me->last_rx_pos) + current_pos;

    /* ---------- 收帧入 FIFO ---------- */
    if (frame_len > 0 && frame_len <= BSP_USART_FRAME_SIZE &&
        me->rx_fifo.count < me->rx_fifo.depth)
    {
        copy_len = (frame_len > me->rx_fifo.frame_max)
                 ? me->rx_fifo.frame_max : frame_len;

        pos = me->last_rx_pos;

        /* 从环形 rx_buf 拷贝到 FIFO 槽 */
        for (i = 0; i < copy_len; i++)
            me->rx_fifo.slots[me->rx_fifo.head].data[i] =
                me->rx_buf[(pos + i) % BSP_USART_FRAME_SIZE];

        me->rx_fifo.slots[me->rx_fifo.head].len = copy_len;
        me->rx_fifo.head = (me->rx_fifo.head + 1) % me->rx_fifo.depth;
        me->rx_fifo.count++;
        me->rx_cnt  = copy_len;
        me->rx_complete = 1;
    }

    me->last_rx_pos = current_pos;
}


/* ============================================================================
 * 轮询处理（建议 1ms 周期调用）
 * ============================================================================ */

void bsp_usart_proc(struct bsp_usart *me)
{
    if (!me || !me->initialized)
        return;
    bsp_usart_tx_inquire(me);   /* 检查 DMA 发送完成 */
    bsp_usart_err_check(me);    /* 检查 ORE 溢出 */
}


/* ============================================================================
 * 格式化打印
 * ============================================================================ */

void bsp_usart_printf(struct bsp_usart *me, const char *fmt, ...)
{
    va_list args;
    char buf[512];
    int len;

    if (!me || !me->initialized)
        return;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len <= 0 || len >= (int)sizeof(buf))
        return;

    /* 通过轮询发送 */
    bsp_usart_send_poll(me, (const uint8_t *)buf, (uint16_t)len);
}
