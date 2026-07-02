/**
 ****************************************************************************************************
 * @file        ExtHardwareTest.h
 * @brief       外部硬件测试模块（F407 版）
 * @note        通过 USART1 接收协议命令，执行外部硬件测试
 *              协议格式：$Test,CMD,SUB[,参数]#，大小写不敏感
 ****************************************************************************************************
 */

#ifndef EXT_HARDWARE_TEST_H
#define EXT_HARDWARE_TEST_H

/**
 * @brief  周期处理：检查 USART1 帧接收，解析并执行测试命令
 * @note   在定时器回调中周期性调用（如 1ms tick）
 */
void ext_hw_test_proc(void);

/* 子命令参数字符串缓存 */
extern char g_test_sub[32];     /* 子命令（如 A1/B1 等引脚名） */
extern char g_test_param[128];  /* 内容/单参数 */

#endif /* EXT_HARDWARE_TEST_H */
