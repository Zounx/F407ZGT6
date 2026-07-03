#include "ff.h"


/**
 * @brief       获得时间
 * @param       mf  : 内存首地址
 * @retval      时间
 * @note        时间编码规则如下:
 *              User defined function to give a current time to fatfs module 
 *              31-25: Year(0-127 org.1980), 24-21: Month(1-12), 20-16: Day(1-31)
 *              15-11: Hour(0-23), 10-5: Minute(0-59), 4-0: Second(0-29 *2) 
 */
DWORD get_fattime (void)
{
    return 0;
}

/* FF_USE_LFN=1，使用 FIL/DIR 内嵌的静态缓冲，不再需要动态分配 */



