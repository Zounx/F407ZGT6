# ============================================================================
#  F407_Source — CMake 构建脚本
#  支持 GCC (arm-none-eabi) 和 Keil (ARMCC/AC6) 双工具链
#  验证使用 GCC 编译，Keil 用户直接用 .uvprojx 打开
#
#  用法（GCC）：
#    cmake -B build -G "Ninja" -DCMAKE_C_COMPILER=arm-none-eabi-gcc
#    cmake --build build
#
#  Keil 用户直接打开 F407_Source.uvprojx 即可
# ============================================================================

cmake_minimum_required(VERSION 3.20)

# ============================================================================
#  工具链检测（必须在 project() 之前）
#  用户通过 -DCMAKE_C_COMPILER=arm-none-eabi-gcc 指定 GCC 工具链
#  不指定则默认 Keil MDK (ARMCC/AC6) 编译
# ============================================================================
if(CMAKE_C_COMPILER MATCHES "arm-none-eabi-")
    # GCC 交叉编译模式：跳过可执行链接测试
    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_SYSTEM_PROCESSOR arm)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    if(NOT CMAKE_ASM_COMPILER)
        set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
    endif()
endif()

project(F407_Source ASM C)

# ============================================================================
#  全局编译定义（Keil 和 GCC 共用）
# ============================================================================

add_definitions(-DSTM32F407xx -DUSE_HAL_DRIVER)

# ============================================================================
#  包含路径（Keil 和 GCC 共用）
# ============================================================================

include_directories(
    ${PROJECT_SOURCE_DIR}/User/Include
    ${PROJECT_SOURCE_DIR}/BSP/Include
    ${PROJECT_SOURCE_DIR}/Middlewares/FATFS/Include
    ${PROJECT_SOURCE_DIR}/Middlewares/FATFS/FATFS_app
    ${PROJECT_SOURCE_DIR}/Middlewares/WIZnet/Include
    ${PROJECT_SOURCE_DIR}/Middlewares/WIZnet/WIZnet_app
    ${PROJECT_SOURCE_DIR}/APP/Communication/Include
    ${PROJECT_SOURCE_DIR}/Platform/STM32F4xx_HAL_Driver/Inc
    ${PROJECT_SOURCE_DIR}/Platform/STM32F4xx_HAL_Driver/Inc/Legacy
    ${PROJECT_SOURCE_DIR}/Platform/CMSIS/Device/ST/STM32F4xx/Include
    ${PROJECT_SOURCE_DIR}/Platform/CMSIS/Core/Include
)

# ============================================================================
#  源文件列表
# ============================================================================

# --- 启动文件 ---
set(STARTUP_SOURCES
    Platform/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f407xx.s
)

# --- BSP 驱动层 ---
set(BSP_SOURCES
    BSP/Source/bsp_adc.c
    BSP/Source/bsp_gpio.c
    BSP/Source/bsp_iic.c
    BSP/Source/bsp_sdio_sd.c
    BSP/Source/bsp_spi.c
    BSP/Source/bsp_sram.c
    BSP/Source/bsp_tim.c
    BSP/Source/bsp_usart.c
)

# --- 用户应用层 ---
set(USER_SOURCES
    User/Source/Main.c
    User/Source/stm32f4xx_it.c
    User/Source/scheduler.c
    User/Source/ExtHardwareTest.c
    Platform/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c
)

# --- HAL 驱动层 ---
set(HAL_SOURCES
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dac.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dac_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_iwdg.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rtc.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rtc_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sd.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sram.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_usart.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_wwdg.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fmc.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fsmc.c
    Platform/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_sdmmc.c
)

# --- FATFS 中间件层 ---
set(FATFS_SOURCES
    Middlewares/FATFS/Source/ff.c
    Middlewares/FATFS/Source/ffunicode.c
    Middlewares/FATFS/Source/ffsystem.c
    Middlewares/FATFS/Source/diskio.c
    Middlewares/FATFS/Source/exfuns.c
    Middlewares/FATFS/FATFS_app/SD.c
)

# --- WIZnet 中间件层 ---
set(WIZNET_SOURCES
    Middlewares/WIZnet/Source/w6100.c
    Middlewares/WIZnet/Source/wiz_socket.c
    Middlewares/WIZnet/Source/wizchip_conf.c
    Middlewares/WIZnet/Source/wiz_loopback.c
    Middlewares/WIZnet/WIZnet_app/spi_w6100.c
    Middlewares/WIZnet/WIZnet_app/ETH_WIZdemo.c
    Middlewares/WIZnet/WIZnet_app/WIZnet_TcpClient.c
    Middlewares/WIZnet/WIZnet_app/WIZnet_TcpSever.c
    Middlewares/WIZnet/WIZnet_app/WIZnet_UDP.c
)

# --- FDS 应用层 ---
set(FDS_SOURCES
    APP/Communication/Source/fds_data.c
    APP/Communication/Source/fds_param.c
    APP/Communication/Source/fds_protocol.c
    APP/Communication/Source/fds_handler.c
)

# ============================================================================
#  目标
# ============================================================================

add_executable(${PROJECT_NAME}.elf
    ${STARTUP_SOURCES}
    ${BSP_SOURCES}
    ${USER_SOURCES}
    ${FATFS_SOURCES}
    ${WIZNET_SOURCES}
    ${FDS_SOURCES}
    ${HAL_SOURCES}
)

# ============================================================================
#  编译器标志（GCC 专用）
# ============================================================================

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # CPU 架构
    target_compile_options(${PROJECT_NAME}.elf PRIVATE
        -mcpu=cortex-m4
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -mthumb
    )

    # C 语言编译选项
    target_compile_options(${PROJECT_NAME}.elf PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-format-truncation
        -Werror=implicit-function-declaration
        -ffunction-sections
        -fdata-sections
        -std=c99
        -O2
    )

    # 汇编编译选项
    target_compile_options(${PROJECT_NAME}.elf PRIVATE
        $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>
    )

    # 链接选项
    target_link_options(${PROJECT_NAME}.elf PRIVATE
        -mcpu=cortex-m4
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -mthumb
        -specs=nano.specs
        -specs=nosys.specs
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map
        -T${PROJECT_SOURCE_DIR}/STM32F407ZG_FLASH.ld
    )
endif()

# ============================================================================
#  自定义目标：生成 .hex 和 .bin（仅 GCC）
# ============================================================================

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
        COMMAND arm-none-eabi-objcopy -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
        COMMAND arm-none-eabi-objcopy -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
        COMMENT "Generating .hex and .bin files"
    )
endif()

# ============================================================================
#  烧录：OpenOCD + ST-Link
# ============================================================================

set(OPENOCD_SCRIPT "interface/stlink.cfg")
set(OPENOCD_TARGET "target/stm32f4x.cfg")

add_custom_target(flash
    COMMAND openocd -f ${OPENOCD_SCRIPT} -f ${OPENOCD_TARGET}
        -c "program ${PROJECT_NAME}.elf verify; reset; exit"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flashing via OpenOCD (ST-Link)..."
)

add_custom_target(debug
    COMMAND openocd -f ${OPENOCD_SCRIPT} -f ${OPENOCD_TARGET}
        -c "program ${PROJECT_NAME}.elf verify reset"
        -c "arm semihosting enable"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flash + start GDB server..."
)
