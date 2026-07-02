/**
 * @file    bsp_gpio.h
 * @brief   GPIO 驱动模块（F407 版，OOP 风格）
 *
 * ============================================================================
 *  快速开始
 * ============================================================================
 *
 * // 1. 定义 GPIO 对象
 * static struct bsp_gpio s_led;
 *
 * // 2. 初始化
 * bsp_gpio_init(&s_led, GPIOB, GPIO_PIN_0, GPIO_MODE_OUTPUT_PP);
 *
 * // 3. 使用
 * bsp_gpio_set(&s_led, true);     // 输出高电平
 * bsp_gpio_toggle(&s_led);        // 电平翻转
 * bool val = bsp_gpio_get(&s_led);// 读取电平
 *
 * // 4. 反初始化
 * bsp_gpio_deinit(&s_led);
 *
 * ============================================================================
 *  支持的 GPIO 端口（F407 共 9 个）
 * ============================================================================
 *
 *   GPIOA ~ GPIOI 均支持，每个端口最多 16 个引脚（PIN_0 ~ PIN_15）
 */

#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>


/* ============================================================================
 * 内部类型（私有字段，用户勿直接访问成员）
 * ============================================================================ */

/**
 * GPIO 对象
 *
 * 每个 GPIO 引脚定义一个此结构体变量。
 * 所有操作通过函数完成，不要直接读写字段。
 */
struct bsp_gpio {
    GPIO_TypeDef *port;         /**< GPIO 端口基址，如 GPIOA / GPIOB */
    uint16_t      pin;          /**< 引脚号，如 GPIO_PIN_0 / GPIO_PIN_1 */
    bool          initialized;  /**< init 成功后为 true */
};


/* ============================================================================
 * API 函数
 * ============================================================================ */

/**
 * @brief  初始化 GPIO 引脚
 *
 * 完成以下工作：
 *   1. 使能对应 GPIO 端口的时钟（自动识别 GPIOA~GPIOI）
 *   2. 初始输出低电平（避免上电瞬间不确定状态）
 *   3. 调用 HAL_GPIO_Init 配置模式、上下拉、速度
 *   4. 标记对象已初始化
 *
 * @param me     GPIO 对象指针
 * @param port   GPIO 端口，如 GPIOA、GPIOB、...、GPIOI
 * @param pin    引脚号，如 GPIO_PIN_0 ~ GPIO_PIN_15
 * @param mode   GPIO 模式（HAL 宏），例如：
 *               - GPIO_MODE_OUTPUT_PP        推挽输出
 *               - GPIO_MODE_OUTPUT_OD        开漏输出
 *               - GPIO_MODE_INPUT            浮空输入
 *               - GPIO_MODE_AF_PP            复用推挽
 *               - GPIO_MODE_AF_OD            复用开漏
 *               - GPIO_MODE_ANALOG           模拟模式
 *               - GPIO_MODE_IT_RISING        上升沿中断
 *               - GPIO_MODE_IT_FALLING       下降沿中断
 *               - GPIO_MODE_IT_RISING_FALLING 双边沿中断
 *
 * @return 0  成功
 * @return -1 me/port 为 NULL
 *
 * @note 上下拉和速度默认使用 NOPULL 和 VERY_HIGH，
 *       如需自定义请初始化后手动调用 HAL_GPIO_Init 覆盖。
 */
int  bsp_gpio_init(struct bsp_gpio *me, GPIO_TypeDef *port,
                   uint16_t pin, uint32_t mode);

/**
 * @brief  设置 GPIO 输出电平
 *
 * @param me     GPIO 对象指针
 * @param state  true=高电平，false=低电平
 * @return 0  成功
 * @return -1 me 为 NULL 或未初始化
 */
int  bsp_gpio_set(struct bsp_gpio *me, bool state);

/**
 * @brief  读取 GPIO 输入电平
 *
 * @param me  GPIO 对象指针
 * @return true  高电平
 * @return false 低电平（或 me 无效）
 */
bool bsp_gpio_get(struct bsp_gpio *me);

/**
 * @brief  翻转 GPIO 输出电平
 *
 * @param me  GPIO 对象指针
 * @return 0  成功
 * @return -1 me 为 NULL 或未初始化
 */
int  bsp_gpio_toggle(struct bsp_gpio *me);

/**
 * @brief  反初始化 GPIO 引脚
 *
 * 调用 HAL_GPIO_DeInit 恢复引脚为默认状态，清空对象字段。
 *
 * @param me  GPIO 对象指针
 * @return 0  成功
 * @return -1 me 为 NULL 或未初始化
 */
int  bsp_gpio_deinit(struct bsp_gpio *me);


#endif /* BSP_GPIO_H */
