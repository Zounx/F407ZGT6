/**
 ******************************************************************************
 * @file      STM32F407ZG_FLASH.ld
 * @author    CMSIS / GCC
 * @brief     Linker script for STM32F407ZG (1MB Flash, 128KB+64KB RAM)
 *
 *            STM32F407ZG 内存布局：
 *              Flash:  0x08000000 - 0x080FFFFF (1MB)
 *              SRAM1:  0x20000000 - 0x2001FFFF (128KB)
 *              SRAM2:  0x10000000 - 0x1000FFFF (64KB, CCM)
 ******************************************************************************
 */

/* 入口点 */
ENTRY(Reset_Handler)

/* 内存区域定义 */
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    SRAM1  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
    SRAM2  (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

/* 栈和堆大小 */
_estack = ORIGIN(SRAM1) + LENGTH(SRAM1);  /* 栈顶 = SRAM1 末尾 */

MIN_HEAP_SIZE  = 0x200;
MIN_STACK_SIZE = 0x400;

/* 段定义 */
SECTIONS
{
    /* 中断向量表（放在最前面） */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* 代码段 */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* 只读数据段（常量） */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* ARM 异常处理表 */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* 初始化的数据段（从 Flash 拷贝到 SRAM1） */
    _sidata = .;
    .data : AT(_sidata)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM1

    /* 未初始化数据段（BSS，在 SRAM1 中） */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > SRAM1

    /* 用户堆栈 */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + MIN_HEAP_SIZE;
        . = . + MIN_STACK_SIZE;
        . = ALIGN(8);
    } > SRAM1

    /* SRAM2（CCM）—— 可放置零延时数据 */
    .sram2 (NOLOAD) :
    {
        . = ALIGN(4);
        *(.sram2)
        *(.sram2*)
        . = ALIGN(4);
    } > SRAM2

    /* 移除不需要的段 */
    /DISCARD/ :
    {
        libc.a (*)
        libm.a (*)
        libgcc.a (*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
