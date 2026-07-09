# CSV 异步写入机制说明

## 1. 概述

F407 平台通过外挂 SRAM（FSMC Bank3, `0x68000000`，1MB）作为 CSV 数据临时缓冲区，实现 **非阻塞式 SD 卡写入**。每 tick 生成一批数据并立即写入，避免一次性填充大缓冲区导致调度器长时间阻塞。

**串口触发指令**：`$Test,SD,CSV#`

## 2. 架构与数据流

```
CPU 生成 CSV 行
      │
      ▼
外挂 SRAM (0x68000000, 512KB 工作区)
      │  └─ 每 tick 最多使用 CSV_TICK_BYTES 字节
      ▼
f_write() → FATFS → disk_write() → sd_write_disk()
      │                                   │
      │                          HAL_SD_WriteBlocks_DMA()
      │                                   │
      │                          DMA 传输完成 (轮询等待)
      │                                   │
      │                          HAL_SD_GetCardState() (等待卡编程)
      │                                   │
      ▼                                  ▼
   SD 卡                             返回上层
```

F4 的 DMA2 可直接访问 FSMC 总线，SRAM 中的数据无需再拷贝到内部 SRAM。

## 3. 状态机

```
┌─────────┐  下一 tick    ┌──────────┐  所有点生成完毕   ┌──────────┐
│CSV_OPEN ├─────────────►│ CSV_TICK ├────────────────►│ CSV_DONE │
│ (空跑)  │              │ × N      │                 │ (打印统计)│
└─────────┘              └──────────┘                 └────┬─────┘
                                                            │
                                                            ▼
                                                     ┌──────────┐
                                                     │CSV_CLOSE │
                                                     │(f_close) │
                                                     └────┬─────┘
                                                          │
                                                          ▼
                                                     ┌──────────┐
                                                     │ CSV_IDLE │
                                                     └──────────┘
```

### 状态说明

| 状态 | 行为 | 耗时 |
|------|------|------|
| `CSV_OPEN` | 空跑一 tick。`sd_test_csv()` 中的 `f_open()` + 写头在串口命令处理 tick 中已执行，此状态确保第一个 `CSV_TICK` 与文件打开操作不在同一 tick | ~0ms |
| `CSV_TICK` | **核心状态**：生成 `CSV_TICK_BYTES` 数据到 SRAM，立即 `f_write` 写入 SD 卡。重复直到 40000 点全部写完 | 取决于 `CSV_TICK_BYTES` |
| `CSV_DONE` | 读取文件大小，计算耗时/速度，打印统计信息 | ~0ms |
| `CSV_CLOSE` | `f_close()` 刷写 FATFS 缓存（目录项、FAT 表），单独一 tick 避免阻塞其他任务 | ~25ms |
| `CSV_IDLE` | 空闲等待状态 | - |

## 4. 数据拆分策略

关键配置项：

```c
#define CSV_POINTS      40000       /* 总数据点 */
#define CSV_BUF_SIZE    524288      /* SRAM 工作区大小 (512KB) */
#define CSV_TICK_BYTES  262144      /* 每 tick 最多处理的数据量 (256KB) */
```

### 核心思路：GEN + WRITE 合并到一个 tick

传统的拆分方案是"填满 → 分 chunk 写入"：

```
V1（淘汰）:
  CSV_GEN:  填满 512KB buffer (单 tick 耗时 ~20ms)
  CSV_WRITE: 分 4KB chunk 逐个 tick 写入 (128 个 tick)
  → 总耗时 = 4 × (20ms + 128 × 10ms) ≈ 5200ms
  → 问题: 写入 chunk 太多，每个 chunk 等一个 10ms tick 间隔
```

优化后：

```
V2（当前方案）:
  CSV_TICK: 生成 CSV_TICK_BYTES → 立即 f_write
  → 每个 tick 内完成 GEN+WRITE，无 tick 间隔等待浪费
  → 总耗时 = 写入次数 × 调度器周期
```

### 每次 tick 的工作量

```
GEN 阶段:
  生成 CSV_TICK_BYTES 字节，约 CSV_TICK_BYTES / 42 行
  （每行约 42 bytes，「Index,Position,Force,Analog,Time,Speed,Voltage\n」）

WRITE 阶段:
  f_write(sram_addr, len) → 底层拆成 512 字节扇区 → SDIO DMA 传输
  DMA 完成后 HAL_SD_GetCardState() 等待卡内部编程完成
```

## 5. 缓冲区大小与调度器周期的关系

### 参数调优数据（实测 @ F407 168MHz, SD 24MHz 4-bit）

| `CSV_TICK_BYTES` | 单 tick 耗时 | 占空比 | 调度器周期 | 写入次数 | 总耗时 | 写入速度 |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| 16KB | ~6ms | 60% | 10ms | 106 次 | ~1206ms | 1381 KB/s |
| 64KB | ~19ms | 63% | 30ms | 28 次 | ~826ms | 2016 KB/s |
| 128KB | ~38ms | 76% | 50ms | 15 次 | ~740ms | 2250 KB/s |
| **256KB** | **~77ms** | **86%** | **90ms** | **7 次** | **~703ms** | **2369 KB/s** |
| 512KB | ~155ms | 78% | 200ms | 4 次 | ~984ms | 1692 KB/s |

### 规律

- **CSV_TICK_BYTES 越大** → 写入次数越少 → tick 间隔等待越少 → 总耗时越短
- **CSV_TICK_BYTES 越大** → 单 tick 耗时越久 → 需要更大的调度器周期来容纳
- **占空比 = 单 tick 耗时 / 调度器周期**，建议 ≤ 85% 以确保有足够余量
- 256KB@90ms 是当前最优平衡点（速度最快、调度器安全）

### 总耗时估算公式

```
CSV 总数据量 ≈ CSV_POINTS × 42 bytes ≈ 1,705,607 bytes
写入次数 ≈ ceil(1,705,607 / CSV_TICK_BYTES)
总耗时 ≈ 写入次数 × 调度器周期 + f_close(≈25ms) + 头尾开销(≈20ms)
```

### 调度器负载

当单 tick 耗时超过调度器周期时：

- 其他任务会**延迟一个周期**执行
- 过载 **≤ 20ms** 时，调度器下一个周期自动恢复
- 持续过载（每个 tick 都超过周期）会导致永久滞后

## 6. 调优指南

### 调整步骤

1. 修改 `SD.c` 中的 `CSV_TICK_BYTES`
2. 修改 `scheduler.c` 中 `ext_hw_test_proc` 的周期
3. 编译烧录，观察串口日志中的 `SCHED` 行确认单 tick 耗时
4. 根据"占空比 ≤ 85%"原则调整

### 选择建议

| 场景 | 推荐配置 | 理由 |
|------|---------|------|
| 追求最快速度 | 256KB / 90ms | 总耗时 703ms，速度 2369 KB/s |
| 调度器负载敏感 | 128KB / 50ms | 单 tick 38ms，余量 12ms |
| 实时任务繁重 | 64KB / 30ms | 单 tick 19ms，余量 11ms |
| 极少写入，最小侵入 | 512KB / 200ms | 只写 4 次，其余时间完全空闲 |

## 7. 注意事项

- **SRAM 容量**：外挂 SRAM 实际可用 1MB，`CSV_BUF_SIZE` 设为 512KB，留出另一半给其他模块
- **f_close 延迟**：FATFS 在 `f_close()` 时刷新所有缓存，耗时约 25ms，已通过单独 `CSV_CLOSE` 状态解耦
- **f_open 延迟**：文件创建首次耗时约 17ms，由 `CSV_OPEN` 状态确保不与 CSV_TICK 挤在同一 tick
- **DMA 中断**：SDIO DMA 中断处理函数是必须的（`DMA2_Stream3_IRQHandler` / `DMA2_Stream6_IRQHandler`）
- **卡状态轮询**：`HAL_SD_WriteBlocks_DMA` 后必须轮询 `HAL_SD_GetCardState` 等待卡内部编程完成
