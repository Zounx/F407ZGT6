#include "scheduler.h"
#include "bsp_tim.h"
#include "bsp_usart.h"
#include "ExtHardwareTest.h"
#include "fds_handler.h"

/* 压制 GCC 10 对空数组初始化的 -Warray-bounds 假阳性警告 */
#if defined(__GNUC__) && !defined(__ARMCC_VERSION)
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

/* ============================================================================
 * 系统时钟配置
 * ============================================================================ */

/**
 * @brief  系统时钟配置
 * @note   默认使用 HSE 25MHz -> PLL -> 168MHz
 *         由 scheduler_init() 内部调用，无需手动调用
 */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* 使能 HSE 振荡器，配置 PLL */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;          /* HSE 25MHz / 25 = 1MHz */
  RCC_OscInitStruct.PLL.PLLN = 336;         /* 1MHz * 336 = 336MHz */
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2; /* 336MHz / 2 = 168MHz (SYSCLK) */
  RCC_OscInitStruct.PLL.PLLQ = 7;           /* 336MHz / 7 = 48MHz (USB/SDIO) */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    for (;;); /* 系统时钟配置失败，死机 */
  }

  /* 配置 SYSCLK, HCLK, PCLK1, PCLK2 */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;    /* HCLK = 168MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;     /* APB1 = 42MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;     /* APB2 = 84MHz */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    for (;;); /* 系统时钟配置失败，死机 */
  }
}


/* ============================================================================
 * 任务调度
 * ============================================================================ */

/**
 * @brief  任务控制块
 * @param  task_func  任务函数指针
 * @param  rate_ms    执行周期（单位：ms）
 * @param  last_run   上次执行的时间戳（单位：ms）
 */
typedef struct {
    void (*task_func)(void);  /**< 任务函数指针 */
    uint32_t rate_ms;         /**< 执行周期（ms） */
    uint32_t last_run;        /**< 上次执行的时间戳（ms） */
} task_t;

static uint8_t task_num;      /**< 任务总数 */


/**
 * @brief  任务表
 * @note   在此添加需要周期性执行的任务
 */
static task_t scheduler_task[] = {
	{ext_hw_test_proc,10,0},
	{ETH_WIZdemo_task,1,0},
	{HandlerTask,20,0},
	{WIZnet_TcpClient_task,200,0},
};


/**
 * @brief  调度器初始化
 * @note   配置系统时钟（HSE 25MHz -> PLL -> 168MHz），
 *         然后计算任务数量，在 main() 初始化阶段调用
 */
void scheduler_init(void) {
    SystemClock_Config();                                   /* 配置系统时钟 */
    task_num = sizeof(scheduler_task) / sizeof(task_t);  /* 计算任务数量 */
}

/**
 * @brief  调度器主循环
 * @note   遍历所有任务，到期则执行，在主循环中轮询调用
 */
void scheduler_run(void) {
    uint32_t now_time = HAL_GetTick();  /* 入口一次采样 */
    for (uint8_t i = 0; i < task_num; i++) {
        /* 判断任务是否到期 */
        if (now_time - scheduler_task[i].last_run >= scheduler_task[i].rate_ms) {
            scheduler_task[i].last_run = now_time;  /* 更新时间戳 */
            scheduler_task[i].task_func();          /* 执行任务 */
            now_time = HAL_GetTick();               /* 任务可能耗时，刷新时间戳 */
        }
    }
}


/* ============================================================================
 * 硬件定时器管理（TIM1~14，F407 全部定时器）
 * ============================================================================
 *
 *   高级控制定时器（APB2，168MHz，16位）：
 *     TIM1, TIM8       - 支持 PWM/捕获/刹车，此处仅用更新中断
 *
 *   通用定时器：
 *     TIM2/5           - 32位通用（APB1）
 *     TIM3/4           - 16位通用（APB1）
 *     TIM9/10/11       - 16位通用（APB2）
 *     TIM12/13/14      - 16位通用（APB1）
 *
 *   基本定时器：
 *     TIM6/7           - 16位基本（APB1），仅更新中断
 *
 *   共享中断向量对（同一个 ISR 处理两个定时器）：
 *     TIM1_BRK_TIM9    → TIM9（TIM1 Break 未使用）
 *     TIM1_UP_TIM10    → TIM1 Update + TIM10
 *     TIM1_TRG_COM_TIM11 → TIM11（TIM1 TRG/COM 未使用）
 *     TIM8_BRK_TIM12   → TIM12（TIM8 Break 未使用）
 *     TIM8_UP_TIM13    → TIM8 Update + TIM13
 *     TIM8_TRG_COM_TIM14 → TIM14（TIM8 TRG/COM 未使用）
 */

/* ---- 定时器对象 ---- */
static struct bsp_tim s_tim1;   /**< TIM1 高级定时器（APB2） */
static struct bsp_tim s_tim2;   /**< TIM2 32位通用定时器 */
static struct bsp_tim s_tim3;   /**< TIM3 16位通用定时器 */
static struct bsp_tim s_tim4;   /**< TIM4 16位通用定时器 */
static struct bsp_tim s_tim5;   /**< TIM5 32位通用定时器 */
static struct bsp_tim s_tim6;   /**< TIM6 基本定时器（1ms 系统心跳） */
static struct bsp_tim s_tim7;   /**< TIM7 基本定时器 */
static struct bsp_tim s_tim8;   /**< TIM8 高级定时器（APB2） */
static struct bsp_tim s_tim9;   /**< TIM9 通用定时器（APB2） */
static struct bsp_tim s_tim10;  /**< TIM10 通用定时器（APB2） */
static struct bsp_tim s_tim11;  /**< TIM11 通用定时器（APB2） */
static struct bsp_tim s_tim12;  /**< TIM12 通用定时器（APB1） */
static struct bsp_tim s_tim13;  /**< TIM13 通用定时器（APB1） */
static struct bsp_tim s_tim14;  /**< TIM14 通用定时器（APB1） */

/* 外部 USART1 对象声明 */
extern struct bsp_usart s_usart1;

/* ---- 回调函数 ---- */

/** @brief TIM6 回调：1ms 心跳，轮询 USART DMA 和测试命令 */
static void tim6_cb(void)
{
    bsp_usart_proc(&s_usart1);
}

/** @brief TIM2 回调：250us 子周期（预留） */
static void tim2_sub_cb(void)
{
    /* TODO: 添加子周期任务（步号分发等） */
}

/** @brief TIM7 回调：50ms 周期（预留） */
static void tim7_cb(void)
{
    /* TODO: 添加 50ms 周期任务 */
}

/** @brief TIM3 回调：50ms 周期（预留） */
static void tim3_cb(void)
{
    /* TODO: 添加 50ms 周期任务 */
}

/** @brief TIM4 回调：50ms 周期（预留） */
static void tim4_cb(void)
{
    /* TODO: 添加 50ms 周期任务 */
}

/** @brief TIM5 回调：1000ms 周期（预留） */
static void tim5_cb(void)
{
    /* TODO: 添加秒级任务 */
}

/** @brief TIM1 回调：预留 */
static void tim1_cb(void) {}

/** @brief TIM8 回调：预留 */
static void tim8_cb(void) {}

/** @brief TIM9 回调：预留 */
static void tim9_cb(void) {}

/** @brief TIM10 回调：预留 */
static void tim10_cb(void) {}

/** @brief TIM11 回调：预留 */
static void tim11_cb(void) {}

/** @brief TIM12 回调：预留 */
static void tim12_cb(void) {}

/** @brief TIM13 回调：预留 */
static void tim13_cb(void) {}

/** @brief TIM14 回调：预留 */
static void tim14_cb(void) {}

/* ---- 中断服务函数 ---- */

/* 独立中断向量定时器 */
void TIM2_IRQHandler(void)      { bsp_tim_irq_handler(&s_tim2); }
void TIM3_IRQHandler(void)      { bsp_tim_irq_handler(&s_tim3); }
void TIM4_IRQHandler(void)      { bsp_tim_irq_handler(&s_tim4); }
void TIM5_IRQHandler(void)      { bsp_tim_irq_handler(&s_tim5); }
void TIM6_DAC_IRQHandler(void)  { bsp_tim_irq_handler(&s_tim6); }
void TIM7_IRQHandler(void)      { bsp_tim_irq_handler(&s_tim7); }
void TIM1_CC_IRQHandler(void)   { bsp_tim_irq_handler(&s_tim1); }
void TIM8_CC_IRQHandler(void)   { bsp_tim_irq_handler(&s_tim8); }

/* 共享中断向量：依次处理两个定时器（内部会跳过未初始化的对象） */
void TIM1_BRK_TIM9_IRQHandler(void)
{
    /* TIM1 Break 未使用，只处理 TIM9 */
    bsp_tim_irq_handler(&s_tim9);
}

void TIM1_UP_TIM10_IRQHandler(void)
{
    bsp_tim_irq_handler(&s_tim1);   /* TIM1 更新中断 */
    bsp_tim_irq_handler(&s_tim10);
}

void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
    /* TIM1 TRG/COM 未使用，只处理 TIM11 */
    bsp_tim_irq_handler(&s_tim11);
}

void TIM8_BRK_TIM12_IRQHandler(void)
{
    /* TIM8 Break 未使用，只处理 TIM12 */
    bsp_tim_irq_handler(&s_tim12);
}

void TIM8_UP_TIM13_IRQHandler(void)
{
    bsp_tim_irq_handler(&s_tim8);   /* TIM8 更新中断 */
    bsp_tim_irq_handler(&s_tim13);
}

void TIM8_TRG_COM_TIM14_IRQHandler(void)
{
    /* TIM8 TRG/COM 未使用，只处理 TIM14 */
    bsp_tim_irq_handler(&s_tim14);
}


/**
 * @brief  定时器初始化
 * @note   注册 TIM1~14 全部定时器，配置周期和回调后启动中断
 * @return 0=成功，非0=失败（返回具体失败定时器编号）
 */
int scheduler_tim_init(void)
{
    int ret;

    /* ----------------------------------------
     *  TIM6 基本定时器 - 1ms 系统心跳
     *  bsp_usart_proc + ext_hw_test_proc
     * ---------------------------------------- */
    ret = bsp_tim_init(&s_tim6, TIM6, 1, tim6_cb, 1);
    if (ret) return -6;

    /* ----------------------------------------
     *  TIM2 32位通用 - 250us 子周期
     * ---------------------------------------- */
    ret = bsp_tim_init_us(&s_tim2, TIM2, 250, tim2_sub_cb, 0);
    if (ret) return -2;

    /* ----------------------------------------
     *  TIM7 / TIM3 / TIM4 - 50ms
     * ---------------------------------------- */
    ret = bsp_tim_init(&s_tim7, TIM7, 50, tim7_cb, 2);
    if (ret) return -7;

    ret = bsp_tim_init(&s_tim3, TIM3, 50, tim3_cb, 2);
    if (ret) return -3;

    ret = bsp_tim_init(&s_tim4, TIM4, 50, tim4_cb, 2);
    if (ret) return -4;

    /* ----------------------------------------
     *  TIM5 32位通用 - 1000ms
     * ---------------------------------------- */
    ret = bsp_tim_init(&s_tim5, TIM5, 1000, tim5_cb, 4);
    if (ret) return -5;

    /* ----------------------------------------
     *  TIM1 / TIM8 高级定时器 - 100ms
     * ---------------------------------------- */
    ret = bsp_tim_init(&s_tim1, TIM1, 100, tim1_cb, 3);
    if (ret) return -1;

    ret = bsp_tim_init(&s_tim8, TIM8, 100, tim8_cb, 3);
    if (ret) return -8;

    /* ----------------------------------------
     *  TIM9~14 通用定时器 - 100ms
     * ---------------------------------------- */
    ret = bsp_tim_init(&s_tim9, TIM9, 100, tim9_cb, 3);
    if (ret) return -9;

    ret = bsp_tim_init(&s_tim10, TIM10, 100, tim10_cb, 3);
    if (ret) return -10;

    ret = bsp_tim_init(&s_tim11, TIM11, 100, tim11_cb, 3);
    if (ret) return -11;

    ret = bsp_tim_init(&s_tim12, TIM12, 100, tim12_cb, 3);
    if (ret) return -12;

    ret = bsp_tim_init(&s_tim13, TIM13, 100, tim13_cb, 3);
    if (ret) return -13;

    ret = bsp_tim_init(&s_tim14, TIM14, 100, tim14_cb, 3);
    if (ret) return -14;

    /* 总清所有定时器 NVIC 挂起（安全冗余） */
    NVIC_ClearPendingIRQ(TIM1_UP_TIM10_IRQn);
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_ClearPendingIRQ(TIM4_IRQn);
    NVIC_ClearPendingIRQ(TIM5_IRQn);
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    NVIC_ClearPendingIRQ(TIM8_UP_TIM13_IRQn);
    NVIC_ClearPendingIRQ(TIM1_BRK_TIM9_IRQn);
    NVIC_ClearPendingIRQ(TIM8_BRK_TIM12_IRQn);
    NVIC_ClearPendingIRQ(TIM8_TRG_COM_TIM14_IRQn);

    return 0;
}
