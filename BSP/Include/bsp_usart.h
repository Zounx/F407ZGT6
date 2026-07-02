/**
 * @file    bsp_usart.h
 * @brief   USART 驱动模块（F407 版，DMA + IDLE 定帧 + FIFO 功能，OOP 风格）
 *
 * ============================================================================
 *  快速开始
 * ============================================================================
 *
 * // 1. 定义 USART 对象（全局或 static）
 * static struct bsp_usart  s_usart1;
 *
 * // 2. 初始化（引脚、波特率、DMA 流都通过参数注入）
 * //    最后三个参数：FIFO深度、单槽大小、内存池（NULL=用板载8×1024）
 * bsp_usart_init(&s_usart1, USART1, GPIOA, GPIO_PIN_9, GPIOA, GPIO_PIN_10,
 *                115200, DMA2_Stream7, DMA2_Stream2,
 *                8, 1024, NULL);
 *
 * // 3. 轮询发送
 * bsp_usart_send_poll(&s_usart1, (uint8_t *)"Hello\r\n", 7);
 *
 * // 4. 格式化打印
 * bsp_usart_printf(&s_usart1, "temp=%.1f\r\n", 25.3);
 *
 * // 5. 1ms 周期轮询（检查 DMA 发送完成 + 溢出恢复）
 * bsp_usart_proc(&s_usart1);
 *
 * // 6. 中断中调用（stm32f4xx_it.c 的 USART1_IRQHandler 里）
 * bsp_usart_irq_handler(&s_usart1);
 *
 * // 7. 主循环查收数据（IDLE 中断已把帧推入 FIFO）
 * if (bsp_usart_is_rx_complete(&s_usart1)) {
 *     uint8_t buf[256];
 *     uint16_t len = bsp_usart_recv_frame(&s_usart1, buf, sizeof(buf));
 *     // 处理 buf[0..len-1] ...
 * }
 *
 * ============================================================================
 *  F4 vs H7 差异要点
 * ============================================================================
 *
 * - F4 无 D-Cache：无 SCB_CleanDCache / SCB_InvalidateDCache 调用
 * - F4 USART 寄存器：SR（状态）替代 ISR，DR 替代 RDR/TDR，无 ICR
 * - F4 无 DMAMUX：通过 DMA_SxCR_CHSEL 选择通道，USART 固定 CH4
 * - F4 HAL_UART_Init 无 ClockPrescaler / OneBitSampling / AdvancedInit
 *
 * ============================================================================
 *  多实例说明
 * ============================================================================
 *
 * - 支持 USART1/2/3/UART4，每个实例独立
 * - 不调 bsp_usart_init() 的实例不会占用 GPIO/时钟，完全不生效
 * - DMA 流参数传 NULL 表示不用 DMA
 * - 多个 USART 共用同一个 NVIC 优先级分组即可
 *
 * ============================================================================
 *  引脚与 DMA 映射（F407）
 * ============================================================================
 *
 * 实例    TX                RX                TX DMA          RX DMA
 * USART1  PA9 (AF7)        PA10 (AF7)        DMA2_Stream7    DMA2_Stream2
 * USART2  PA2 (AF7)        PA3 (AF7)         DMA1_Stream6    DMA1_Stream5
 * USART3  PB10 (AF7)       PB11 (AF7)        DMA1_Stream3    DMA1_Stream1
 * UART4   PA0 (AF8)        PA1 (AF8)         DMA1_Stream4    DMA1_Stream2
 *
 * ============================================================================
 *  FIFO 内存说明
 * ============================================================================
 *
 * fifo_pool 参数有三种传法：
 *
 * ── 1. NULL · 板载模式 ──────────────────────────────────────────
 *    使用 struct 内嵌的 _def_slots[8] + _def_data[8][1024]。
 *    depth ≤ 8，frame_max ≤ 1024，免配内存。
 *
 *    用法：
 *      bsp_usart_init(&s_u1, USART1, ..., 8, 1024, NULL);
 *
 * ── 2. static 数组 ───────────────────────────────────────────────
 *    数组默认分配在 .bss 段，容量不受板载限制。
 *
 *    用法：
 *      static uint8_t s_pool[BSP_USART_FIFO_POOL_SIZE(16, 4096)];
 *      bsp_usart_init(&s_u4, UART4, ..., 16, 4096, s_pool);
 *
 * ── 外部内存池布局（供手动分配参考） ──────────────────────────────
 *    fifo_pool 指向的内存块布局：
 *      [0 .. depth*sizeof(slot)-1]              → slot 数组
 *      [depth*sizeof(slot) .. + depth*frame_max] → 帧数据池
 *    可用宏 BSP_USART_FIFO_POOL_SIZE(depth,frame_max) 计算总大小。
 *
 * ── 容量参考 ───────────────────────────────────────────────────────
 *    板载默认 8×1024 = 8KB，每个 USART 实例约 10KB 总占用。
 */

#ifndef BSP_USART_H
#define BSP_USART_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdarg.h>


/* ============================================================================
 * 配置常量
 *
 * BSP_USART_FRAME_SIZE      环形 DMA 接收缓冲区大小（字节）
 *                             必须 >= 预期最大帧长
 * BSP_USART_FIFO_DEF_DEPTH  板载默认 FIFO 深度（8 槽）
 * BSP_USART_FIFO_DEF_FRAME  板载默认单槽大小（1024 字节）
 * BSP_USART_FIFO_POOL_SIZE  外部内存池总大小计算宏
 * ============================================================================ */
#define BSP_USART_FRAME_SIZE      1024
#define BSP_USART_FIFO_DEF_DEPTH     8
#define BSP_USART_FIFO_DEF_FRAME     1024

/** 计算外部 FIFO 内存池总大小（slot 数组 + 帧数据池） */
#define BSP_USART_FIFO_POOL_SIZE(depth, frame_max) \
    ((uint32_t)(depth) * sizeof(struct bsp_usart_slot) + \
     (uint32_t)(depth) * (uint32_t)(frame_max))


/* ============================================================================
 * F407 DMA 通道选择（所有 USART 固定 CH4 = CHSEL_2）
 * ============================================================================ */
#define BSP_USART_DMA_CHSEL      DMA_SxCR_CHSEL_2


/* ============================================================================
 * 内部类型
 *
 * 以下 struct 的字段定义在 .h 中是给编译器看的。
 * 你只需要定义变量（static struct bsp_usart s_xxx;）并通过函数操作它，
 * 不要直接读写 me->xxx 字段，除非你看过实现代码。
 * ============================================================================ */

/** FIFO 单帧存储槽 */
struct bsp_usart_slot {
    uint8_t  *data;          /**< 指向帧数据的指针 */
    uint16_t  len;           /**< 帧有效长度 */
};

/** FIFO 环形队列（先进先出） */
struct bsp_usart_fifo {
    struct bsp_usart_slot *slots;  /**< 槽数组（指针，板载或外部池） */
    uint8_t  depth;                /**< 槽数 */
    uint16_t frame_max;            /**< 单槽最大字节 */
    volatile uint8_t head;         /**< 写入位置（生产者） */
    volatile uint8_t tail;         /**< 读取位置（消费者） */
    volatile uint8_t count;        /**< 队列中帧数 */
};

/**
 * USART 对象
 *
 * 每个 USART 实例定义一个此结构体变量，
 * 所有函数第一个参数传 &s_xxx。
 *
 * 字段说明（仅供阅读，不要直接赋值）：
 */
struct bsp_usart {
    /* -------- 硬件配置（init 时写入，之后只读） -------- */
    USART_TypeDef    *instance;      /**< USART1 / USART2 / USART3 / UART4 */
    GPIO_TypeDef     *tx_port;       /**< TX 引脚所属 GPIO 组 */
    uint16_t          tx_pin;        /**< TX 引脚号，如 GPIO_PIN_9 */
    GPIO_TypeDef     *rx_port;       /**< RX 引脚所属 GPIO 组 */
    uint16_t          rx_pin;        /**< RX 引脚号 */
    uint32_t          baudrate;      /**< 波特率 */

    /* -------- DMA 流（init 时写入，传 NULL 表示不用 DMA） -------- */
    DMA_Stream_TypeDef *dma_tx;      /**< TX DMA 流 */
    DMA_Stream_TypeDef *dma_rx;      /**< RX DMA 流 */

    /* -------- 收发状态（内部维护，不要手动改） -------- */
    volatile uint8_t  dmaing;        /**< DMA 发送进行中标志 */
    uint16_t          tx_type;       /**< 发送类型：0=空闲，1=DMA 进行中 */
    uint16_t          tx_sum;        /**< 本次 DMA 发送总字节数 */
    uint16_t          tx_cnt;        /**< 已发送字节计数 */
    uint16_t          rx_cnt;        /**< 最近一次 IDLE 中断收到的字节数 */
    uint16_t          rx_sum;        /**< 累计接收字节（未使用） */
    uint16_t          last_rx_pos;   /**< DMA 接收环形缓冲区上次读位置 */
    uint8_t           rx_complete;   /**< 有帧到达标志（irq_handler 置位） */
    uint32_t          rx_timestamp;  /**< 最近接收时间戳（未使用） */

    /* -------- 收发缓冲区 -------- */
    uint8_t           rx_buf[BSP_USART_FRAME_SIZE]; /**< DMA 环形接收缓冲区 */
    uint8_t           tx_buf[BSP_USART_FRAME_SIZE]; /**< DMA 发送缓冲区 */

    /* -------- FIFO 帧队列 -------- */
    struct bsp_usart_fifo rx_fifo;   /**< IDLE 中断收集的帧队列 */

    /* -------- 板载 FIFO 后备存储（fifo_pool==NULL 时使用） -------- */
    struct bsp_usart_slot _def_slots[BSP_USART_FIFO_DEF_DEPTH];
    uint8_t               _def_data[BSP_USART_FIFO_DEF_DEPTH][BSP_USART_FIFO_DEF_FRAME];

    /* -------- 状态 -------- */
    bool              initialized;   /**< init 成功后为 true */
};


/* ============================================================================
 * 初始化 / 反初始化
 * ============================================================================ */

/**
 * @brief  初始化 USART
 *
 * 完成以下工作：
 *   1. 使能 GPIO 时钟，配置 TX/RX 为复用推挽
 *   2. 使能 USART 时钟
 *   3. 调用 HAL_UART_Init 设置 8N1 + 指定波特率
 *   4. 使能 IDLE 中断（用于 DMA 接收定帧）
 *   5. 配置 RX/TX DMA（PAR、M0AR、CR 方向、CHSEL）
 *   6. 清零收发缓冲区和 FIFO
 *
 * @param me        USART 对象指针
 * @param instance  USART 外设基址，如 USART1 / USART2 / USART3 / UART4
 * @param tx_port   TX 引脚 GPIO 组，如 GPIOA
 * @param tx_pin    TX 引脚号，如 GPIO_PIN_9
 * @param rx_port   RX 引脚 GPIO 组，如 GPIOA
 * @param rx_pin    RX 引脚号，如 GPIO_PIN_10
 * @param baudrate  波特率，如 115200 / 921600
 * @param dma_tx    TX DMA 流指针，不用传 NULL
 * @param dma_rx    RX DMA 流指针，不用传 NULL
 * @param fifo_depth    FIFO 槽数（如 8 / 16）
 * @param fifo_frame_max 单槽最大字节（如 1024 / 4096）
 * @param fifo_pool   外部内存池指针，NULL=用板载存储（depth≤8, frame_max≤1024）
 *
 * @return 0  成功
 * @return -1 me / instance / tx_port / rx_port 为 NULL
 * @return -2 GPIO 时钟使能失败（只支持 A/B/C/D 组）
 * @return -3 不支持的 USART 实例（只支持 USART1/2/3 + UART4）
 * @return -4 HAL_UART_Init 失败
 * @return -5 FIFO 配置超出板载容量且未提供外部内存池
 *
 * @note 调用前确保 NVIC 已配置 USART 中断优先级
 * @note 同一实例重复调用是安全的，但会重新配置 GPIO 和时钟
 * @note 多个 USART 实例只需分别定义 struct 并各自调用 init
 */
int  bsp_usart_init(struct bsp_usart *me, USART_TypeDef *instance,
                    GPIO_TypeDef *tx_port, uint16_t tx_pin,
                    GPIO_TypeDef *rx_port, uint16_t rx_pin,
                    uint32_t baudrate,
                    DMA_Stream_TypeDef *dma_tx, DMA_Stream_TypeDef *dma_rx,
                    uint8_t fifo_depth, uint16_t fifo_frame_max,
                    void *fifo_pool);

/**
 * @brief  反初始化 USART
 *
 * @param me  USART 对象指针
 * @return 0  成功
 * @return -1 me 为 NULL 或未 init
 */
int  bsp_usart_deinit(struct bsp_usart *me);


/* ============================================================================
 * 轮询收发
 *
 * send_poll 会阻塞直到全部发送完成。
 * recv_poll 会阻塞直到接收到指定字节数或超时。
 * ============================================================================ */

/**
 * @brief  轮询模式发送（阻塞直到完成）
 *
 * @param me   USART 对象指针
 * @param data 待发送数据
 * @param len  字节数
 * @return 0  成功
 * @return -1 me/data 为 NULL 或未 init
 */
int  bsp_usart_send_poll(struct bsp_usart *me, const uint8_t *data, uint16_t len);

/**
 * @brief  发送字符串常量（编译期算长度，零运行时开销）
 *
 * 用法：
 *   bsp_usart_send_str(&s_usart1, "Hello\r\n");
 *
 * @note 只能传字符串字面量，不能传 char* 指针
 */
#define bsp_usart_send_str(me, str) \
    bsp_usart_send_poll((me), (const uint8_t *)(str), sizeof(str) - 1)

/**
 * @brief  轮询模式接收（阻塞直到收够或超时）
 *
 * @param me         USART 对象指针
 * @param data       接收缓冲区
 * @param len        期望接收的字节数
 * @param timeout_ms 超时毫秒
 * @return 0   成功
 * @return -1  me/data 为 NULL 或未 init
 * @return -2  超时
 */
int  bsp_usart_recv_poll(struct bsp_usart *me, uint8_t *data, uint16_t len, uint32_t timeout_ms);


/* ============================================================================
 * DMA 发送
 *
 * 流程：
 *   1. send_dma 将数据拷贝到 tx_buf，启动 DMA 传输
 *   2. DMA 完成后硬件自动置 TC 标志（F4 在 SR 寄存器）
 *   3. tx_inquire 在 proc 中轮询检查 NDTR && TC，清理状态
 *
 * 注意：F4 无 D-Cache，无需 Clean/Invalidate 操作
 * ============================================================================ */

/**
 * @brief  DMA 模式发送（非阻塞）
 *
 * 将数据拷贝到内部 tx_buf，启动 DMA 流发送。
 * 如果上次 DMA 还没完成，会等待最多约 10000 轮循环，
 * 超时则强制复位 DMA 流。
 *
 * @param me   USART 对象指针
 * @param data 待发送数据
 * @param len  字节数（超过 BSP_USART_FRAME_SIZE 会被截断）
 *
 * @note 必须在 init 时传入有效的 dma_tx，否则此函数无效果
 * @note 发送完成后需调用 tx_inquire 或 proc 清理状态
 */
void bsp_usart_send_dma(struct bsp_usart *me, uint8_t *data, uint16_t len);

/**
 * @brief  查询 DMA 发送是否忙碌
 *
 * @param me  USART 对象指针
 * @return 0  空闲
 * @return 1  DMA 发送进行中
 */
uint8_t bsp_usart_is_tx_busy(struct bsp_usart *me);

/**
 * @brief  强制清空发送缓冲区并停止 DMA
 *
 * @param me  USART 对象指针
 */
void bsp_usart_clr_tx(struct bsp_usart *me);

/**
 * @brief  查询 DMA 发送完成状态（非阻塞）
 *
 * 检查 NDTR==0 && TC 标志，如果完成则清理状态。
 * 通常在 bsp_usart_proc() 中自动调用。
 *
 * @param me  USART 对象指针
 */
void bsp_usart_tx_inquire(struct bsp_usart *me);


/* ============================================================================
 * 帧接收（IDLE 中断 + FIFO）
 *
 * 工作流程：
 *   1. DMA 持续把数据写入 rx_buf（环形）
 *   2. 发完一帧后总线空闲，硬件触发 IDLE 中断
 *   3. irq_handler 计算本次帧长，拷贝到 FIFO
 *   4. 主循环查 is_rx_complete，有数据则 recv_frame 取出
 *
 * 需要外部准备：
 *   - DMA 流已正确配置（循环模式 NDTR = BSP_USART_FRAME_SIZE）
 *   - USART 中断已在 NVIC 使能
 *   - stm32f4xx_it.c 的 USARTx_IRQHandler 中调 bsp_usart_irq_handler
 * ============================================================================ */

/**
 * @brief  从 FIFO 取一帧（非阻塞）
 *
 * @param me   USART 对象指针
 * @param data 接收缓冲区
 * @param size 缓冲区大小
 * @return 实际收到的字节数（0 = FIFO 空，无帧）
 */
uint16_t bsp_usart_recv_frame(struct bsp_usart *me, uint8_t *data, uint16_t size);

/**
 * @brief  检查 FIFO 中是否有帧
 *
 * @param me  USART 对象指针
 * @return 0  FIFO 空
 * @return 1  有帧可读
 */
uint8_t  bsp_usart_is_rx_complete(struct bsp_usart *me);

/**
 * @brief  获取 FIFO 队首帧长度
 *
 * @param me  USART 对象指针
 * @return 帧长度，FIFO 空则返回 0
 */
uint16_t bsp_usart_get_rx_len(struct bsp_usart *me);

/**
 * @brief  清空接收缓冲区和 FIFO，重启 DMA 接收
 *
 * @param me  USART 对象指针
 */
void     bsp_usart_clr_rx(struct bsp_usart *me);

/**
 * @brief  检查并恢复 ORE（溢出错误）
 *
 * @param me  USART 对象指针
 */
void     bsp_usart_err_check(struct bsp_usart *me);


/* ============================================================================
 * 中断处理
 *
 * 在 stm32f4xx_it.c 的 USARTx_IRQHandler 中调用：
 *
 *   void USART1_IRQHandler(void)
 *   {
 *       bsp_usart_irq_handler(&s_usart1);
 *   }
 *
 * irq_handler 完成：
 *   - IDLE 中断定帧：计算帧长，拷贝到 FIFO
 *   - ORE 恢复：自动重启 RX DMA
 * ============================================================================ */

/**
 * @brief  USART 中断处理入口
 *
 * 必须在 USARTx_IRQHandler 中调用。
 * 只处理 IDLE 中断（定帧）和 ORE（溢出恢复），
 * 不处理 RXNE/TXE 等常规中断。
 *
 * @param me  USART 对象指针
 *
 * @note 需要 DMA 接收流已配置（dma_rx != NULL）才能定帧
 */
void bsp_usart_irq_handler(struct bsp_usart *me);


/* ============================================================================
 * 轮询处理
 *
 * 需要在主循环中以约 1ms 周期调用。
 *
 * void main(void)
 * {
 *     // ... 初始化 ...
 *     while (1) {
 *         bsp_usart_proc(&s_usart1);
 *         bsp_usart_proc(&s_usart2);
 *         // ... 其他任务 ...
 *         HAL_Delay(1);
 *     }
 * }
 * ============================================================================ */

/**
 * @brief  周期轮询处理（建议 1ms 调用一次）
 *
 * 内部依次调用：
 *   1. bsp_usart_tx_inquire  — 检查 DMA 发送完成
 *   2. bsp_usart_err_check   — 检查 ORE 溢出
 *
 * @param me  USART 对象指针
 */
void bsp_usart_proc(struct bsp_usart *me);


/* ============================================================================
 * 格式化打印
 *
 * 用法类似 printf，通过轮询发送。
 * 内部缓冲区 512 字节，超过会截断。
 *
 *   bsp_usart_printf(&s_usart1, "val=%d, str=%s\r\n", 42, "ok");
 * ============================================================================ */

/**
 * @brief  格式化打印（通过轮询发送）
 *
 * @param me  USART 对象指针
 * @param fmt 格式字符串（同 printf 语法）
 * @param ... 可变参数
 */
void bsp_usart_printf(struct bsp_usart *me, const char *fmt, ...);


#endif /* BSP_USART_H */
