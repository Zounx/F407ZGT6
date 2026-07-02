/**
 * @file    bsp_gpio.c
 * @brief   GPIO 驱动模块实现（F407 版）
 */

#include "bsp_gpio.h"


int bsp_gpio_init(struct bsp_gpio *me, GPIO_TypeDef *port,
                  uint16_t pin, uint32_t mode)
{
    GPIO_InitTypeDef cfg = {0};

    if (!me || !port)
        return -1;

    me->port = port;
    me->pin  = pin;

    /* 使能 GPIO 时钟（HAL_GPIO_Init 不负责开时钟） */
    if      (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
    else if (port == GPIOI) __HAL_RCC_GPIOI_CLK_ENABLE();

    /* 初始输出低电平（避免上电瞬间不确定状态） */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);

    cfg.Pin   = pin;
    cfg.Mode  = mode;
    cfg.Pull  = GPIO_NOPULL;
    cfg.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(port, &cfg);

    me->initialized = true;
    return 0;
}


int bsp_gpio_set(struct bsp_gpio *me, bool state)
{
    if (!me || !me->initialized)
        return -1;
    HAL_GPIO_WritePin(me->port, me->pin,
                      state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
}


bool bsp_gpio_get(struct bsp_gpio *me)
{
    if (!me || !me->initialized)
        return false;
    return HAL_GPIO_ReadPin(me->port, me->pin) == GPIO_PIN_SET;
}


int bsp_gpio_toggle(struct bsp_gpio *me)
{
    if (!me || !me->initialized)
        return -1;
    HAL_GPIO_TogglePin(me->port, me->pin);
    return 0;
}


int bsp_gpio_deinit(struct bsp_gpio *me)
{
    if (!me || !me->initialized)
        return -1;
    HAL_GPIO_DeInit(me->port, me->pin);
    me->initialized = false;
    me->port = NULL;
    me->pin  = 0;
    return 0;
}
