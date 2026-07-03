#ifndef FATFS_SD_H
#define FATFS_SD_H

#include "ff.h"

/**
 * @brief  初始化 SD 卡硬件 + FATFS 挂载
 * @note   支持串口指令: $Test,SD,MOUNT#
 *         代码调用: sd_mount();
 * @return FR_OK 成功，其他见 ff.h 错误码
 */
FRESULT sd_mount(void);


/**
 * @brief  写入内容到指定文件（覆盖创建）
 * @param  path    完整路径，如 "0:/test.txt" 或 "0:/data/config.txt"
 * @param  content 要写入的字符串内容
 * @note   支持串口指令: $Test,SD,WRITE,路径,内容#
 *         代码调用: sd_write_file("0:/test.txt", "Hello");
 */
FRESULT sd_write_file(const char *path, const char *content);


/**
 * @brief  读取指定文件内容到缓冲区
 * @param  path   完整路径，如 "0:/test.txt"
 * @param  buf    读取缓冲区
 * @param  buf_sz 缓冲区大小
 * @param  br     输出参数，实际读取字节数
 * @note   支持串口指令: $Test,SD,READ,路径#
 *         代码调用:
 *           char buf[128]; UINT br;
 *           sd_read_file("0:/test.txt", buf, sizeof(buf), &br);
 */
FRESULT sd_read_file(const char *path, void *buf, UINT buf_sz, UINT *br);


/**
 * @brief  创建目录
 * @param  path 目录路径，如 "0:/myfolder" 或 "0:/data/log"
 * @note   支持串口指令: $Test,SD,MKDIR,目录名#
 *         代码调用: sd_mkdir("0:/myfolder");
 */
FRESULT sd_mkdir(const char *path);


/**
 * @brief  创建空文件（文件已存在则返回 FR_EXIST）
 * @param  path 文件路径，如 "0:/new.txt"
 * @note   支持串口指令: $Test,SD,MKFILE,文件名#
 *         代码调用: sd_mkfile("0:/new.txt");
 */
FRESULT sd_mkfile(const char *path);


/**
 * @brief  删除文件或空目录
 * @param  path 要删除的文件/目录路径
 * @note   支持串口指令: $Test,SD,DELETE,路径#
 *         代码调用: sd_delete("0:/old.txt");
 *				 删空目录: sd_delete("0:/myfolder");
 */
FRESULT sd_delete(const char *path);


/**
 * @brief  树形列出目录内容（通过 USART 输出）
 * @param  path 目录路径，传 "0:/" 列根目录，传 "0:/myfolder" 列子目录
 *              传 NULL 等价于 "0:/"
 * @note   支持串口指令: $Test,SD,LIST#
 *         代码调用: sd_list("0:/");
 */
FRESULT sd_list(const char *path);


/* ============================================================================
 * 串口指令测试接口（供 ExtHardwareTest 命令表注册）
 * ============================================================================ */
void sd_test_mount(void);
void sd_test_write(void);
void sd_test_read(void);
void sd_test_mkdir(void);
void sd_test_mkfile(void);
void sd_test_list(void);
void sd_test_delete(void);


#endif /* FATFS_SD_H */
