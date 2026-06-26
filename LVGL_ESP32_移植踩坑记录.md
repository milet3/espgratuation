# LVGL v9.5 移植到 ESP32 踩坑记录

> 硬件：ESP32-S3 + ILI9341 (SPI) + XPT2046 触摸
> LVGL 版本：v9.5
> 日期：2026-06-17 ~ 2026-06-18

---

## 1. 编译错误：字符串跨行

**现象：**
```
warning: missing terminating " character
error: expected ')' before ';' token
```

**原因：** C 语言字符串字面量不能包含物理换行。代码中的 `\n` 被写成了真正的换行符。

**修复：** 将 `esp_rom_printf("...结果=%s\n", ...)` 的字符串合并为一行，用 `\n` 表示换行。

**文件：** `components/LVGL/lv_port_disp.c`

---

## 2. GPIO 238 错误

**现象：**
```
E (1196) gpio: GPIO_PIN mask error
E (1196) gpio: gpio_set_level(238): GPIO output gpio_num error
```

**原因：** `soil_sensor_init()` 中引脚定义为 `-1`（未连接），但代码没有做 `>= 0` 检查就直接执行 `1ULL << -1`——这在 C 中是**未定义行为**，导致 GPIO 驱动访问不存在的引脚。

**修复：** 用 `#if SOIL_UART_POWER_PIN >= 0` 包裹 GPIO 初始化代码。

**文件：** `components/bsp_uart/soil_sensor.c`

---

## 3. SPI 错误：invalid dev handle

**现象：**
```
E (1536) spi_master: check_trans_valid(1083): invalid dev handle
```
每次日志**恰好 2 条**，周期性出现（~30ms 间隔）。

**排查过程：**
- 最初以为是 LCD SPI（SPI2_HOST）的问题
- 尝试了分块发送、启用 DMA、增大 LVGL 任务栈——均无效
- **关键线索：错误总是成对出现**（2 条/次），匹配 XPT2046 触摸读取 X 和 Y 各一次 SPI 事务

**真正原因：** `touch_get_xy()` 使用的 `touch_spi` 句柄从未初始化（`touch_driver_init()` 未被调用），始终为 NULL。LVGL 每次读取输入时调用它，产生 2 次 "invalid dev handle" 错误。

**修复：**
1. `touch_get_xy()` 开头加 `if (touch_spi == NULL) return false;`
2. `lv_port_indev_init()` 中调用 `touch_driver_init()`

**文件：** `components/LVGL/touch_driver.c`、`components/LVGL/lv_port_indev.c`

---

## 4. 编码问题导致 main.c 中文乱码

**现象：**
```
warning: missing terminating " character
error: unterminated argument list invoking macro "ESP_LOGI"
```

**原因：** `main.c` 原始编码为 **UTF-8**，PowerShell `Get-Content -Encoding Default` 将其按 GB2312 解码，写回时又用 UTF-8 编码，导致中文字符变成乱码、字符串跨行。

**修复：**
- 始终用 `[System.Text.Encoding]::UTF8` 读写
- 或使用 Node.js `fs.readFileSync(path, "utf8")` / `fs.writeFileSync(path, content, "utf8")`

**教训：** 在中文 Windows 环境下操作含中文的源文件，必须显式指定 UTF-8 编码。

---

## 5. ILI9341 初始化不完整导致黑屏

**现象：** SPI 通信正常（自检 ESP_OK），但屏幕完全不亮。

**原因：** 原始 `lcd_init_hw()` 只有 4 条指令（Reset → Sleep Out → Pixel Format → Display On），缺少：
- 电源控制（Power Control A/B）
- VCOM 控制
- Gamma 校正（正/负）
- 帧率控制
- Pump Ratio

**修复：** 补全标准 ILI9341 初始化序列（共 ~60 条寄存器写入）。

**文件：** `components/LVGL/lv_port_disp.c` → `lcd_init_hw()`

---

## 6. 字节序问题（Endianness）

**现象：** SPI 通信正常，但屏幕显示扭曲的色带（红绿蓝三色测试变成 6 条混乱色带），颜色也不对。

**原因：** ESP32 是**小端序**（Little-Endian），IL9341 期望**大端序**（Big-Endian）。

| 数据类型 | 问题 |
|----------|------|
| `lcd_write_data8()` | 单字节，无影响 ✅ |
| `lcd_write_data16()`（坐标） | 字节交换：319(0x013F) → ILI9341 收到 0x3F01(16129)，被截断导致窗口错位 |
| `lcd_write_pixels()`（像素） | 每个像素两字节交换：红色 0xF800 → ILI9341 收到 0x00F8（蓝色） |

**修复：**
- 坐标：在 `lcd_set_window()` 调用 `lcd_write_data16()` 前预交换字节
  ```c
  lcd_write_data16((x >> 8) | (x << 8));  // 预交换
  ```
- 像素：在 `lcd_write_pixels()` 内用 swap buffer 逐对交换字节
  ```c
  for (size_t i = 0; i < n; i += 2) {
      sw[i]   = p[i+1];
      sw[i+1] = p[i];
  }
  ```

**文件：** `components/LVGL/lv_port_disp.c`

---

## 7. 屏幕闪烁

**现象：** LVGL demo 能显示，但画面一闪一闪（flickering/tearing）。

**排查：**
- DMA 模式（`SPI_DMA_CH_AUTO`）+ 高速时钟（20MHz）导致 SPI 时序不稳定
- 分块大小过大（4096 字节）超出 polling 模式硬件 FIFO

**修复：**
```c
// SPI 配置
SPI_DMA_DISABLED          // 关闭 DMA，使用 polling 模式
clock_speed_hz = 10*1000*1000  // 降频到 10MHz
max_transfer_sz = 64       // 匹配硬件 FIFO 大小
```
```c
// 分块发送
static uint8_t sw[64];     // swap buffer 64 字节
const size_t ch = 64;      // chunk 64 字节
```

**额外调整：**
- LVGL 刷新率从 5ms 调整到 16ms（~60Hz，匹配屏幕刷新率）
- LVGL 任务栈从 8KB 增大到 16KB

**文件：** `components/LVGL/lv_port_disp.c`、`main/main.c`

---

## 8. 触摸无反应

**现象：** 触摸 SPI 已初始化（`TOUCH: spi=0x...`），轮询正常，但按屏幕无反应。

**状态：** 已添加原始坐标调试日志，等待确认触摸硬件返回的 ADC 值。

**调试命令：** 观察日志中 `TOUCH: raw=XXXX,YYYY` 的值：
- 始终为 0 或 4095 → 硬件未连接/损坏
- 在 200~3800 范围变化 → 触摸正常，需调整校准参数

**文件：** `components/LVGL/touch_driver.c`、`components/LVGL/lv_port_indev.c`

---

## 总结：ESP32 + LVGL 移植检查清单

| # | 检查项 | 常见问题 |
|---|--------|----------|
| 1 | GPIO 定义 | `-1` 引脚必须有 `#if >= 0` 保护 |
| 2 | SPI 时钟 | ILI9341 建议 ≤ 16MHz，稳定用 10MHz |
| 3 | DMA 模式 | polling 模式更稳定，DMA 需确保 buffer 在内置 SRAM |
| 4 | 字节序 | ILI9341 大端序 ≠ ESP32 小端序，16-bit 数据需要字节交换 |
| 5 | ILI9341 初始化 | 必须包含电源/VCOM/Gamma/帧率等完整寄存器配置 |
| 6 | MADCTL 寄存器 | 不同屏幕方向需不同值（0x28/0x48/0x68/0xE8） |
| 7 | 触摸 SPI | 确认 `touch_driver_init()` 在 `lv_port_indev_init()` 中被调用 |
| 8 | 编码 | Windows 下编辑含中文的源文件，确保 UTF-8 编码 |
| 9 | LVGL 任务栈 | 建议 ≥ 16KB |
| 10 | 刷新率 | `vTaskDelay` 建议 10~16ms（60~100Hz） |


## 9. fatal error: atomic.h

ESP-IDF FreeRTOS 的 atomic.h 在 freertos/ 子目录。
lv_freertos.c: #include "atomic.h" -> #include "freertos/atomic.h"

---

## 10. lv_conf.h 宏重复定义

lv_conf_internal.h 已定义 LV_OS_FREERTOS=2。用户 lv_conf.h 只需:
#define LV_USE_OS LV_OS_FREERTOS

---

## 11. 触控无反应：空 Group 绑定

lv_indev_set_group(indev, lv_group_get_default()) 返回 NULL。
删除这行即可。触摸不需要绑定 Group。
文件: lv_port_indev.c

---

## 12. 触控坐标反向

加 TOUCH_FLIP_X / TOUCH_FLIP_Y / TOUCH_SWAP_XY 宏。
文件: lv_port_indev.c

---

## 13. SPI_DEVICE_HALFDUPLEX 不兼容

ESP32-S3 SPI 不支持此 flag，删除。文件: touch_driver.c

---

## 14. SPI txdata transfer > host maximum

max_transfer_sz=512 超 FIFO(64B)。改回 64。文件: lv_port_disp.c

---

## 15. Brownout 触发

BOD Level 7 太敏感。降到 5。WiFi TX power 限 10dBm。
硬件: TFT VCC-GND 并联 100uF+0.1uF 电容。
文件: sdkconfig, wifi_manager.c

---

## 16. esp_wifi_set_max_tx_power 时机

必须在 esp_wifi_start() 后调用。移到 WIFI_EVENT_STA_START。
文件: wifi_manager.c

---

## 17. LV_USE_OS 导致 FPS 暴跌

FreeRTOS 原子操作在 ESP32-S3 上极慢。保持 LV_USE_OS=0。
文件: lv_conf.h

---

## 18. PSRAM draw buffer -> Cache Error

PSRAM 不适合 LVGL 渲染缓冲。放回内部 SRAM。
文件: lv_port_disp.c

---

## 19. git checkout 丢失未提交代码

LVGL 移植代码未 commit，git checkout 恢复旧版。
教训: git stash > git checkout。文件: main.c

---

## 20. LVGL sysmon 乱码

内置 sysmon 不适合小屏。用串口 ESP_LOGI 输出 FPS。

---

## 21. PowerShell Set-Content 破坏 UTF-8

用 [System.IO.File]::ReadAllText/WriteAllText + UTF8Encoding 保编码。

---

## 22. DIRECT 模式 + 全帧缓冲解决触摸花屏

**现象**: LVGL demo 中触摸滑动时画面花屏（撕裂），松手后恢复正常。Arc 控件在拖拽时出现残影/撕裂条带。

**排查过程**:
1. 最初怀疑缓冲区太小（原 1/10 屏幕 = 7680 像素/缓冲），增大到 40 行（12800 像素/缓冲）→ 仍花屏
2. 分析 Arc 控件（150×150px）在屏幕上占 ~115 行，40 行缓冲跨越 3 个条带
3. 拖拽时指示器旧位置/新位置落在不同条带 → 屏幕同时显示新旧画面 → "花屏"

**根因**: `LV_DISPLAY_RENDER_MODE_PARTIAL` + 条带缓冲 = 跨条带撕裂
- Arc 150px 高，任何小于 150 行的缓冲都会导致指示器跨越多个条带
- 每个条带独立 flush，中间没有 vsync，视觉上就是撕裂

**修复**: 改用 `LV_DISPLAY_RENDER_MODE_DIRECT` + 全屏帧缓冲（PSRAM）
```c
// 全屏帧缓冲：320×240×2(RGB565) = 153600 字节
fb = (uint8_t *)heap_caps_malloc(HOR_RES * VER_RES * 2,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
lv_display_set_buffers(disp, fb, NULL, HOR_RES * VER_RES * 2,
                       LV_DISPLAY_RENDER_MODE_DIRECT);
```

**DIRECT 模式原理**:
- LVGL 始终渲染到同一块全帧缓冲，画面永远完整一致
- flush 回调只把脏区推到 LCD，帧缓冲本身不失效
- 不会出现跨条带的不一致画面

**配套改动**:
1. SPI 时钟 10MHz → 40MHz（ILI9341 可承受）
2. SPI DMA 启用（`SPI_DMA_CH_AUTO`）
3. 像素传输用 `spi_device_transmit`（DMA），不再用 polling
4. 逐行字节交换：LVGL 存小端 [lo,hi]，ILI9341 要 [hi,lo]

**踩坑**: `lv_color_t` 在 LVGL v9 中是 RGB888（3 字节，`{blue, green, red}`），没有 `.full` 成员。帧缓冲实际存 RGB565（2 字节），所以 fb 应声明为 `uint8_t *`，手动按字节寻址和交换。

**文件**: `components/LVGL/lv_port_disp.c`
---

## 23. LV_USE_PERF_MONITOR 编译失败：undefined reference to lv_os_get_idle_percent

**现象**: 开启 `LV_USE_PERF_MONITOR` 后链接报错：
```
undefined reference to `lv_os_get_idle_percent'
```

**根因**: `LV_SYSMON_GET_IDLE` 宏默认展开为 `lv_os_get_idle_percent`，该函数只在 `LV_USE_OS != 0` 时存在。由于 `LV_USE_OS=0`（开启会掉帧，见第 17 条），链接失败。

**排查**:
1. 先试 `#define LV_SYSMON_GET_IDLE() 0` → 编译错误，因为 `lv_sysmon.c` 中有函数前向声明 `uint32_t LV_SYSMON_GET_IDLE(void);` 被展开成 `uint32_t 0;`
2. `LV_SYSMON_GET_IDLE` 是函数名替换宏（不是数值宏），需指向有效函数名

**修复**: 
1. `lv_conf.h`：`#define LV_SYSMON_GET_IDLE  lv_sysmon_get_idle_stub`
2. `lv_port_disp.c`：添加空桩函数
```c
uint32_t lv_sysmon_get_idle_stub(void) {
    return 0;  /* LV_USE_OS=0，无真实 idle 数据 */
}
```

**副作用**: CPU 占用率恒显示 100%，但 FPS 显示正常。

**文件**: `components/LVGL/lv_conf.h`、`components/LVGL/lv_port_disp.c`
---

## 24. 触摸灵敏度优化：多点采样 + 压力检测 + 去抖

**现象**: 默认触摸驱动仅做单次 X/Y 读取，任何非零值即判定为按下。轻触/快速点击时响应不稳定，容易漏触或误触。

**根因**:
1. 无多点采样平均 -> 噪声导致单次读数偏差
2. 无压力检测（Z1/Z2）-> 无法区分轻触和误触
3. 无状态去抖 -> 快速按/松时抖动

**修复**:

**touch_driver.c** -- 底层驱动关键参数：
```
TOUCH_SAMPLES       = 2     // 多点采样数（2=低延迟，4=更平滑）
PRESSURE_THRESHOLD  = 15    // 压力阈值，越小越灵敏（0~4095）
TOUCH_SPI_SPEED_HZ  = 1500000  // SPI 速率 1.5MHz（XPT2046 最大 2MHz）
```
- `touch_get_xy()` 改为循环采样 TOUCH_SAMPLES 次，取平均 X/Y/Z
- 使用 Z1/Z2 计算触摸压力：`pressure = z2 * raw_x / z1`
- 仅当 `avg_pressure > PRESSURE_THRESHOLD` 时判定为按下
- 指针为 NULL 时返回 last_x/last_y 保持坐标连续

**lv_port_indev.c** -- LVGL 输入适配关键参数：
```
TOUCH_X_MIN    = 50     // 校准最小值（原 200，边缘触摸更灵敏）
TOUCH_X_MAX    = 4000   // 校准最大值（原 3800）
TOUCH_Y_MIN    = 50
TOUCH_Y_MAX    = 4000
DEBOUNCE_COUNT = 1      // 去抖次数（1=即时，2=防抖）
```
- 校准范围从 [200,3800] 扩展到 [50,4000]，边缘触摸识别更好
- 引入状态机：连续 N 次同状态才切换 pressed/released
- 按下态保留 last_x/last_y，释放态沿用最后位置

**调优指南**:
| 参数 | 降低 | 提高 |
|------|------|------|
| PRESSURE_THRESHOLD | 更灵敏（轻触可触发） | 更防误触 |
| TOUCH_SAMPLES | 更低延迟 | 更平滑/抗噪 |
| DEBOUNCE_COUNT | 即时响应 | 防抖动 |

**文件**: `components/LVGL/touch_driver.c`、`components/LVGL/lv_port_indev.c`

---

## 25. 帧率优化：从 30 FPS 提升到 60 FPS

**现象**: LVGL 画面流畅度不够，尤其动画/拖拽时明显卡顿。

**根因分析**（4 个瓶颈，按影响排序）:

| 瓶颈 | 位置 | 影响 |
|------|------|------|
| LV_DEF_REFR_PERIOD = 33ms | lv_conf.h | **硬限制 30 FPS** |
| 逐像素字节交换（C 循环 ~153K 次/帧） | lv_port_disp.c | CPU 瓶颈 |
| SPI 速率仅 40MHz | lv_port_disp.c | 像素传输慢 |
| LVGL 堆仅 48KB | lv_conf.h | 渲染缓冲不足 |

**修复**:

**lv_conf.h**（3 个宏改动）：
```
LV_DEF_REFR_PERIOD        = 16       // 33->16ms，60 FPS 上限
LV_MEM_SIZE               = (64*1024) // 48->64KB，更多渲染缓冲
LV_DRAW_BUF_STRIDE_ALIGN  = 1        // 64->1，DIRECT 模式无需对齐
```

**lv_port_disp.c**（核心改动）：
```
// SPI 速率 40MHz -> 60MHz
LCD_SPI_SPEED_HZ = (60 * 1000 * 1000)

// 关键：设置 RGB565_SWAPPED 颜色格式
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
```
- **关键优化**: `LV_COLOR_FORMAT_RGB565_SWAPPED` 让 LVGL 以 ILI9341 的大端序 [hi,lo] 写入帧缓冲，**彻底消除逐像素字节交换循环**
- flush 回调中 `tx_buffer` 直接指向帧缓冲行，无需中间 swap buffer
- 每帧节省 ~153,600 次字节操作

**效果对比**:
| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 帧率上限 | 30 FPS | 60 FPS |
| 字节交换 | CPU 循环 ~153K 次/帧 | 0（LVGL 自动处理） |
| SPI 速率 | 40 MHz | 60 MHz |
| LVGL 堆 | 48 KB | 64 KB |

**注意事项**:
- 60MHz SPI 需要短排线 + 良好布线，若花屏则降回 40MHz
- `LV_COLOR_FORMAT_RGB565_SWAPPED` 是 LVGL v9 内置格式，确认版本 >= v9.0
- DIRECT 模式 + PSRAM 帧缓冲不变（见第 22 条）

**文件**: `components/LVGL/lv_conf.h`、`components/LVGL/lv_port_disp.c`
