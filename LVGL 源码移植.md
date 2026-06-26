---
tags:
  - LVGL
  - 移植
  - STM32
  - 嵌入式
  - GUI
  - 图形库
created: 2026-06-13
---

# LVGL 源码移植到 STM32

本文介绍如何从 GitHub 下载 LVGL 源码并手动移植到 STM32 工程中（Keil / STM32CubeIDE），适用于 CubeMX 不提供 LVGL 图形中间件的场景，或需要使用最新版本 / 自定义裁剪的情况。

> 如果你使用 STM32F4/F7/H7 且想快速上手，可参考 [[环境配置|LVGL 环境配置]] 和 [[显示驱动移植|LVGL 显示驱动移植]]。

---

## 一、LVGL 是什么

LVGL（Light and Versatile Graphics Library）是一个开源嵌入式图形库，专为资源受限的 MCU 设计。

### 1.1 它能做什么

| 功能 | 说明 | 典型应用 |
|------|------|----------|
| **30+ 内置控件** | 按钮、标签、列表、图表、滑块、开关、键盘等 | 仪表盘、设置页、菜单 |
| **灵活布局** | Flex、Grid 布局系统，自动适配不同分辨率 | 自适应 UI、横竖屏切换 |
| **样式系统** | 类 CSS 的样式继承，支持状态（默认/按下/聚焦/禁用） | 主题切换、品牌化 UI |
| **动画系统** | 60 FPS 动画，支持缓动曲线、路径动画、时间线 | 页面切换、加载动画 |
| **多显示支持** | 同时驱动多块屏幕 | 主屏+副屏、仪表盘双屏 |
| **输入设备** | 触摸屏、编码器、键盘、按键 | 电阻屏、电容屏、旋钮 |
| **字体与图片** | TTF/OTF 矢量字体、PNG/JPG/GIF、Lottie 动画 | 多语言、图标、启动动画 |
| **文件系统** | 对接 FATFS / LittleFS，从 SD 卡读资源 | 图片资源外置、主题文件 |

### 1.2 为什么不直接用 CubeMX 方式

| 场景 | 说明 |
|------|------|
| CubeMX 不内置 LVGL | LVGL 本身不是 ST 官方中间件，CubeMX 的 X-CUBE-DISPLAY 包版本老旧且绑定 TouchGFX |
| 需要最新版本 | 官方 X-CUBE 包可能使用 LVGL v7/v8，而最新 v9 有大量改进 |
| 需要自定义裁剪 | 手动移植可精确控制哪些模块编译，哪些控件启用 |
| 跨平台复用 | 自己管理的源码可在不同 MCU（STM32、ESP32、NXP）间直接复用 |
| 学习底层原理 | 理解 LVGL 的显示/输入/时钟驱动机制，便于深度优化 |

### 1.3 关键特性

- **纯 C 实现**：零外部依赖，可移植到任何 32 位 MCU
- **模块化裁剪**：通过 `lv_conf.h` 宏开关，不使用的控件不编译
- **驱动抽象层**：仅需实现 `flush_cb`（显示）和 `read_cb`（输入）两个回调
- **硬件加速接口**：支持 DMA2D（STM32 Chrom-ART）、GPU、VGLite
- **最低硬件**：≥ 32KB RAM、≥ 128KB Flash、≥ 240×240 分辨率

---

## 二、从 GitHub 下载源码

### 2.1 获取源码

```
方法 1：下载 Release 压缩包（推荐）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
1. 打开 https://github.com/lvgl/lvgl/releases
2. 找到最新稳定版（如 v9.2.0）
3. 下载 Source code (zip)
4. 解压到本地，如 D:\Lib\lvgl-9.2.0\

方法 2：Git Clone
━━━━━━━━━━━━━━━
git clone --branch v9.2.0 --depth 1 https://github.com/lvgl/lvgl.git
```

### 2.2 源码目录结构

```
lvgl-9.2.0/
├── lvgl.h                  ← 总头文件，用户只需 #include "lvgl.h"
├── lv_conf_template.h      ← ★ 配置模板，复制为 lv_conf.h
├── lv_version.h            ← 版本号宏
│
├── src/
│   ├── core/               ← ★ 核心引擎（必须编译）
│   │   ├── lv_obj.c        ← 基础对象（所有控件的父类）
│   │   ├── lv_group.c      ← 输入分组管理
│   │   ├── lv_refr.c       ← 刷新逻辑
│   │   └── lv_disp.c       ← 显示管理
│   │
│   ├── draw/               ← ★ 渲染引擎（必须编译）
│   │   ├── lv_draw.c       ← 绘制调度
│   │   ├── lv_draw_rect.c  ← 矩形绘制
│   │   ├── lv_draw_label.c ← 文本绘制
│   │   ├── lv_draw_image.c ← 图片绘制
│   │   ├── lv_draw_line.c  ← 线条绘制
│   │   ├── lv_draw_arc.c   ← 圆弧绘制
│   │   └── sw/             ← 纯软件渲染后端
│   │
│   ├── display/            ← ★ 显示驱动接口（必须）
│   │   └── lv_display.c    ← display 对象创建与管理
│   │
│   ├── indev/              ← ★ 输入设备接口（按需）
│   │   ├── lv_indev.c      ← 输入设备管理
│   │   └── lv_indev_scroll.c  ← 滚动逻辑
│   │
│   ├── widgets/            ← 控件库（按需选择）
│   │   ├── lv_button.c     ← 按钮
│   │   ├── lv_label.c      ← 标签
│   │   ├── lv_slider.c     ← 滑块
│   │   ├── lv_chart.c      ← 图表
│   │   ├── lv_list.c       ← 列表
│   │   ├── lv_dropdown.c   ← 下拉框
│   │   ├── lv_textarea.c   ← 文本框
│   │   └── ...（30+ 控件）
│   │
│   ├── layouts/            ← 布局系统（按需）
│   │   ├── lv_flex.c       ← Flex 布局
│   │   └── lv_grid.c       ← Grid 布局
│   │
│   ├── themes/             ← 主题（按需）
│   │   ├── lv_theme_default.c  ← 默认主题
│   │   └── lv_theme_mono.c     ← 单色主题
│   │
│   ├── fonts/              ← 内置字体（按需）
│   │   └── lv_font_montserrat_14.c  ← 默认 14px 字体
│   │
│   ├── libs/               ← 扩展库（按需）
│   │   ├── lv_bmp.c        ← BMP 解码
│   │   ├── lv_png/         ← PNG 解码
│   │   ├── lv_jpg/         ← JPG 解码
│   │   ├── lv_gif/         ← GIF 解码
│   │   ├── lv_freetype/    ← TrueType 字体
│   │   └── lv_qrcode/      ← 二维码生成
│   │
│   └── others/             ← 杂项工具
│       ├── lv_snapshot.c   ← 控件截图
│       ├── lv_anim_timeline.c  ← 动画时间线
│       └── lv_file_explorer.c  ← 文件浏览器
│
├── examples/               ← 示例代码
├── demos/                  ← 官方演示
└── docs/                   ← 文档
```

> **核心理解**：`src/core/` + `src/draw/` + `src/display/` 是 LVGL 的**最小内核**，必须全部编译。`src/widgets/` 下的控件和 `src/layouts/` 下的布局系统可按需选择。

---

## 三、移植步骤

### 3.1 整理源码到工程

在你的 STM32 工程目录下创建 LVGL 目录：

```
YourProject/
├── Core/
│   ├── Inc/
│   └── Src/
├── Drivers/
│   └── ...
├── Middlewares/
│   └── lvgl/                   ← ★ 新建
│       ├── lvgl.h              ← 总头文件
│       ├── lv_conf.h           ← ★ 配置文件（从 lv_conf_template.h 复制）
│       ├── lv_version.h        ← 版本号头文件
│       ├── src/                ← LVGL 全部源码
│       │   ├── core/           ← 核心引擎
│       │   ├── draw/           ← 渲染引擎
│       │   ├── display/        ← 显示接口
│       │   ├── indev/          ← 输入设备接口
│       │   ├── widgets/        ← 控件库
│       │   ├── layouts/        ← 布局系统
│       │   ├── themes/         ← 主题
│       │   ├── fonts/          ← 内置字体
│       │   └── libs/           ← 扩展库
│       └── port/               ← ★ 自己写的平台适配层
│           ├── lv_port_disp.c  ← 显示驱动（flush_cb）
│           ├── lv_port_indev.c ← 输入驱动（read_cb）
│           └── lv_port_tick.c  ← 心跳/时钟
│
└── lv_conf.h                   ← 也可放工程根目录
```

### 3.2 需要编译的源文件

LVGL v9 推荐**直接编译整个 `src/` 目录**而非手动挑选文件，因为模块间依赖紧密。但如果 Flash 紧张，可按需选择：

```
src/
├── core/                 ← ★ 全部编译（这是 LVGL 本体）
│   ├── lv_obj.c
│   ├── lv_obj_class.c
│   ├── lv_obj_style.c
│   ├── lv_obj_tree.c
│   ├── lv_obj_pos.c
│   ├── lv_obj_scroll.c
│   ├── lv_obj_draw.c
│   ├── lv_group.c
│   ├── lv_refr.c
│   ├── lv_disp.c
│   └── lv_others.c       ← 杂项工具（快照、图片等）
│
├── draw/                 ← ★ 全部编译
│   ├── lv_draw.c
│   ├── lv_draw_rect.c
│   ├── lv_draw_label.c
│   ├── lv_draw_image.c
│   ├── lv_draw_line.c
│   ├── lv_draw_arc.c
│   ├── lv_draw_triangle.c
│   ├── lv_draw_vector.c  ← 矢量图形（按需，v9 新增）
│   ├── lv_image_decoder.c
│   │
│   └── sw/               ← 软件渲染后端 ★
│       ├── lv_draw_sw.c
│       ├── lv_draw_sw_rect.c
│       ├── lv_draw_sw_arc.c
│       └── ...
│
├── display/              ← ★ 全部编译
│   └── lv_display.c
│
├── indev/                ← ★ 全部编译（有输入设备的话）
│   ├── lv_indev.c
│   └── lv_indev_scroll.c
│
├── widgets/              ← 按需选择（只用几个控件就只编译那几个）
│   ├── lv_button.c
│   ├── lv_label.c
│   ├── lv_image.c
│   ├── lv_bar.c
│   ├── lv_slider.c
│   ├── lv_switch.c
│   └── ...（按需添加）
│
├── layouts/              ← 按需
│   ├── lv_flex.c
│   └── lv_grid.c
│
├── themes/               ← 至少选一个主题
│   └── lv_theme_default.c
│
├── fonts/                ← 至少选一个字体
│   └── lv_font_montserrat_14.c
│
└── libs/                 ← 按需
    ├── lv_bmp.c          ← BMP 解码
    │── lv_png/lv_png.c   ← PNG 解码（需 lodepng）
    ├── lv_freetype/      ← TTF 字体
    └── lv_qrcode/        ← 二维码

★ = 最小系统必须
```

### 3.3 Keil 工程配置

```
1. 打开 Keil → Options for Target → C/C++

2. Include Paths 添加：
   ..\Middlewares\lvgl
   ..\Middlewares\lvgl\src
   ..\Core\Inc

3. Define 添加（预定义宏）：
   LV_CONF_INCLUDE_SIMPLE

4. 将需要的 .c 文件添加到工程分组：
   - 建一个 "LVGL/core" 分组，加入 src/core/*.c
   - 建一个 "LVGL/draw" 分组，加入 src/draw/*.c
   - 建一个 "LVGL/widgets" 分组，加入需要的控件
   - 建一个 "LVGL/port" 分组，加入 port/*.c

5. C99 Mode：LVGL 用到 C99 特性，确认编译选项支持
```

### 3.4 STM32CubeIDE 配置

```
1. 右键工程 → Properties → C/C++ Build → Settings

2. MCU GCC Compiler → Include paths 添加：
   ../Middlewares/lvgl
   ../Middlewares/lvgl/src

3. MCU GCC Compiler → Preprocessor → Defined symbols 添加：
   LV_CONF_INCLUDE_SIMPLE

4. 将 LVGL src/ 下的 .c 添加到编译（在 CMakeLists.txt 中
   用 GLOB_RECURSE 或手动列出）
```

### 3.5 CMake 配置示例（CubeIDE / CLion）

```cmake
# 方式 A：递归添加 LVGL 全部源码（推荐，最简单）
file(GLOB_RECURSE LVGL_SOURCES
    ${CMAKE_SOURCE_DIR}/Middlewares/lvgl/src/*.c
)

# 方式 B：手动加最小集（Flash 紧张时用）
set(LVGL_CORE
    Middlewares/lvgl/src/core/lv_obj.c
    Middlewares/lvgl/src/core/lv_group.c
    Middlewares/lvgl/src/core/lv_refr.c
    Middlewares/lvgl/src/core/lv_disp.c
    # ... 其他 src/core/*.c
)
set(LVGL_DRAW
    Middlewares/lvgl/src/draw/lv_draw.c
    Middlewares/lvgl/src/draw/sw/lv_draw_sw.c
    # ... 其他 src/draw/**/*.c
)
set(LVGL_PORT
    Middlewares/lvgl/port/lv_port_disp.c
    Middlewares/lvgl/port/lv_port_indev.c
    Middlewares/lvgl/port/lv_port_tick.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    Middlewares/lvgl
    Middlewares/lvgl/src
)

target_sources(${PROJECT_NAME} PRIVATE
    ${LVGL_CORE}
    ${LVGL_DRAW}
    ${LVGL_PORT}
)
```

---

## 四、编写 lv_conf.h

这是移植中**最重要的文件**，决定 LVGL 的所有行为——分辨率、颜色深度、缓冲区大小、启用哪些控件、字体、内存策略等。

```c
/* ============================================================
 * lv_conf.h — STM32 嵌入式 LVGL v9 配置
 * 适用于 STM32F4/F7/H7 + 320×240 TFT + FreeRTOS
 * 从 GitHub lv_conf_template.h 复制后修改
 * ============================================================ */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ── 颜色配置 ── */
#define LV_COLOR_DEPTH          16      /* RGB565（最常用） */
/* 16 = RGB565 | 32 = RGB888/XRGB8888 | 8 = 调色板 | 1 = 单色 */

/* ── 默认显示分辨率 ── */
#define LV_DPI_DEF              130     /* PPI，用于缩放参考 */

/* ── 内存管理 ── */
#define LV_MEM_SIZE             (48 * 1024)  /* LVGL 自身内存池 48KB */
#define LV_MEM_ADR              0x00    /* 0 = 使用 malloc，或指定绝对地址 */
#define LV_MEM_CUSTOM           0       /* 1 = 用户自定义内存分配函数 */
#define LV_MEMCPY_MEMSET_STD    1       /* 1 = 使用标准 memcpy/memset */

/* ── 操作系统集成 ── */
#define LV_USE_OS               0       /* 1 = 使用操作系统 */
#if LV_USE_OS
    #define LV_OS_NONE          0
    #define LV_OS_PTHREAD       0
    #define LV_OS_FREERTOS      1      /* FreeRTOS */
    #define LV_OS_CMSIS_RTX     0
    #define LV_OS_CMSIS_RTX2    0
    #define LV_OS_CUSTOM        0
#endif

/* ── 日志级别 ── */
#define LV_USE_LOG              0      /* 0 = 关闭，开发时打开 LV_LOG_LEVEL_WARN */
#if LV_USE_LOG
    #define LV_LOG_LEVEL        LV_LOG_LEVEL_WARN
#endif

/* ── 性能选项 ── */
#define LV_DRAW_BUF_STRIDE_ALIGN     64  /* 行对齐（DMA 要求） */
#define LV_USE_DRAW_SW                1   /* 软件渲染后端 ★ 必须 */
#define LV_DRAW_SW_SUPPORT_RGB565     1   /* RGB565 支持 */

/* ── 控件开关（按需启用，不用的注释掉省 Flash） ── */
#define LV_USE_BUTTON           1
#define LV_USE_LABEL            1
#define LV_USE_IMAGE            1
#define LV_USE_BAR              1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_ROLLER           1
#define LV_USE_TEXTAREA         1
#define LV_USE_TABLE            1
#define LV_USE_LIST             1
#define LV_USE_CHART            1
#define LV_USE_KEYBOARD         1
#define LV_USE_MSGBOX           1
#define LV_USE_SPINBOX          1
#define LV_USE_TABVIEW          1
#define LV_USE_TILEVIEW         0     /* 少用，注释掉 */
#define LV_USE_WIN              0
#define LV_USE_SPINNER          1
#define LV_USE_LED              1
#define LV_USE_SPAN             0
#define LV_USE_SCALE            0

/* ── 布局系统 ── */
#define LV_USE_FLEX             1     /* Flex 布局 */
#define LV_USE_GRID             1     /* Grid 布局 */

/* ── 主题 ── */
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_MONO       0
#define LV_THEME_DEFAULT_GROW   1     /* 新控件自动继承主题 */

/* ── 字体 ── */
#define LV_FONT_MONTSERRAT_14   1     /* 14px 默认字体 */
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_48   0
/* LV_FONT_DEFAULT = &lv_font_montserrat_14 */

/* ── 图片解码器（按需启用） ── */
#define LV_USE_BMP              1     /* BMP 格式 */
#define LV_USE_PNG              0     /* PNG（需额外 RAM） */
#define LV_USE_JPG              0     /* JPG */
#define LV_USE_GIF              0     /* GIF */
#define LV_USE_SJPG             0     /* Split-JPG（流式） */
#define LV_USE_FREETYPE         0     /* TTF 字体 */

/* ── 动画与特效 ── */
#define LV_USE_ANIM             1     /* 动画系统 */
#define LV_USE_SHADOW           0     /* 阴影（省资源时关闭） */
#define LV_USE_BLEND_MODES      0     /* 混合模式 */

/* ── 杂项 ── */
#define LV_USE_SNAPSHOT         0     /* 截图 */
#define LV_USE_FILE_EXPLORER    0     /* 文件浏览器 */
#define LV_USE_QRCODE           0     /* 二维码 */

/* ── STM32 硬件加速 ── */
/* F4/F7/H7 有 DMA2D（Chrom-ART） */
#define LV_USE_DRAW_DMA2D       0     /* 1 = 启用 DMA2D 加速（需实现回调） */

/* ── 输入设备默认分组 ── */
#define LV_INDEV_DEF_READ_PERIOD    30  /* 输入读取周期 (ms) */

/* ── 帧率 ── */
#define LV_DEF_REFR_PERIOD      33     /* 默认刷新周期 (ms)，约 30 FPS */

#endif /* LV_CONF_H */
```

> **温馨提示**：首次移植建议先开最小配置（只保留 BUTTON、LABEL、默认主题），编译通过后再逐步开启更多控件。

---

## 五、编写平台适配层

这是移植的**核心工作**。LVGL 通过回调函数与硬件交互，你需要实现以下三个接口。

### 5.1 显示驱动 — flush_cb（字节刷屏）

LVGL 渲染完一块像素后，调用 `flush_cb` 把数据发送到屏幕。

```c
/* ============================================================
 * lv_port_disp.c — LVGL 显示驱动适配 STM32 + SPI TFT
 * 核心：实现 flush_cb，把像素缓冲区通过 SPI/DMA 发送
 * ============================================================ */

#include "lvgl.h"
#include "stm32f4xx_hal.h"
#include "lcd_driver.h"     /* 你的屏幕驱动 */

/* ── 全屏双缓冲（最佳方案，RAM 消耗最大） ── */
#define HOR_RES         320
#define VER_RES         240

static lv_color_t buf1[HOR_RES * VER_RES];  /* 320×240×2 = 150KB */
static lv_color_t buf2[HOR_RES * VER_RES];  /* 150KB */

void lv_port_disp_init(void)
{
    /* 1. 创建 display 对象 */
    lv_display_t *disp = lv_display_create(HOR_RES, VER_RES);

    /* 2. 设置缓冲区（双缓冲 + 直接渲染模式） */
    lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    /* 3. 注册 flush_cb */
    lv_display_set_flush_cb(disp, disp_flush_cb);
}

/* ★ flush_cb 回调：LVGL 把渲染好的像素传给你，你负责发送到屏幕 */
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map)
{
    /* area→x1, area→y1, area→x2, area→y2 是刷新区域 */
    uint16_t width  = area->x2 - area->x1 + 1;
    uint16_t height = area->y2 - area->y1 + 1;

    /* 设置屏幕写入窗口 */
    lcd_set_window(area->x1, area->y1, area->x2, area->y2);

    /* DMA 方式发送像素数据 */
    lcd_write_data_dma(px_map, (uint32_t)(width * height * 2));
    /* 注意：DMA 传输完成后再调用 lv_display_flush_ready() */
}

/* ── DMA 传输完成中断回调 ── */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi2) {
        /* ★ 通知 LVGL：数据已经送到屏幕了 */
        lv_display_flush_ready(lv_display_get_default());
    }
}
```

> 更多缓冲区策略（全屏单缓冲、双缓冲、行缓冲）参见 [[显示驱动移植|LVGL 显示驱动移植]]。

### 5.2 输入驱动 — read_cb（触摸 / 编码器）

```c
/* ============================================================
 * lv_port_indev.c — LVGL 输入设备适配
 * 支持：触摸屏（XPT2046/FT6336）、编码器、按键
 * ============================================================ */

#include "lvgl.h"
#include "touch_driver.h"       /* 你的触摸驱动 */

/* ── 触摸屏驱动 ── */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int16_t last_x = 0, last_y = 0;
    uint16_t x, y;
    bool pressed;

    if (touch_get_xy(&x, &y, &pressed)) {
        data->point.x   = x;
        data->point.y   = y;
        data->state     = pressed ? LV_INDEV_STATE_PRESSED :
                                    LV_INDEV_STATE_RELEASED;
        last_x = x; last_y = y;
    } else {
        /* 未读到数据，保持上次坐标 */
        data->point.x   = last_x;
        data->point.y   = last_y;
        data->state     = LV_INDEV_STATE_RELEASED;
    }
}

void lv_port_indev_init(void)
{
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}

/* ── 编码器驱动（旋钮输入） ── */
static int32_t encoder_diff = 0;
static lv_indev_state_t encoder_state = LV_INDEV_STATE_RELEASED;

static void encoder_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->enc_diff  = encoder_diff;
    data->state     = encoder_state;
    encoder_diff    = 0;   /* 读完后清零 */
}

/* 在编码器中断中调用此函数 */
void encoder_isr_handler(int8_t dir)
{
    encoder_diff += dir;
}

/* 按键 GPIO 中断处理 */
void encoder_button_isr_handler(bool pressed)
{
    encoder_state = pressed ? LV_INDEV_STATE_PRESSED :
                              LV_INDEV_STATE_RELEASED;
}
```

> 更多输入设备细节（校准、手势、多指触控）参见 [[输入设备驱动|LVGL 输入设备驱动]]。

### 5.3 心跳时钟 — tick

LVGL 需要一个 1ms 精度的时基来驱动动画和定时器。

```c
/* ============================================================
 * lv_port_tick.c — LVGL 心跳时钟
 * 方式 A：SysTick 中断中调用（裸机下最常用）
 * 方式 B：FreeRTOS 软件定时器（有 OS 时）
 * ============================================================ */

/* ── 方式 A：SysTick 中断 ── */
/* 在 stm32f4xx_it.c 的 SysTick_Handler 中添加： */
void SysTick_Handler(void)
{
    HAL_IncTick();          /* HAL 库需要 */
    lv_tick_inc(1);         /* ★ LVGL 心跳 +1ms */
}

/* ── 方式 B：FreeRTOS 定时器 ── */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void lv_tick_timer_cb(TimerHandle_t xTimer)
{
    lv_tick_inc(1);
}

void lv_port_tick_init(void)
{
    TimerHandle_t timer = xTimerCreate(
        "lv_tick", pdMS_TO_TICKS(1), pdTRUE, NULL, lv_tick_timer_cb
    );
    xTimerStart(timer, 0);
}
```

> **注意**：`lv_tick_inc(1)` 必须在中断安全或任务安全的上下文中调用，不能放在有互斥锁的地方。

---

## 六、硬件加速（DMA2D）

STM32 F4/F7/H7 系列有 DMA2D（Chrom-ART）硬件加速器，可大幅减少 CPU 在像素搬运上的开销。

### 6.1 LVGL v9 DMA2D 集成

```c
/* ============================================================
 * lv_port_dma2d.c — LVGL DMA2D 加速
 * 适用于 STM32F429/F746/H743 等带 DMA2D 的 MCU
 * ============================================================ */

#include "lvgl.h"
#include "stm32f4xx_hal.h"

extern DMA2D_HandleTypeDef hdma2d;  /* CubeMX 生成 */

/* ── 颜色填充加速 ── */
void lv_draw_sw_fill_cb(lv_draw_unit_t *draw_unit,
                         const lv_draw_fill_dsc_t *dsc,
                         const lv_area_t *coords)
{
    uint32_t color = lv_color_to32(dsc->color);
    uint32_t width  = lv_area_get_width(coords);
    uint32_t height = lv_area_get_height(coords);
    uint32_t offset = (coords->y1 * HOR_RES + coords->x1);

    /* 配置 DMA2D 填充 */
    hdma2d.Init.Mode         = DMA2D_R2M;         /* 寄存器到内存 */
    hdma2d.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = HOR_RES - width;
    HAL_DMA2D_Init(&hdma2d);

    /* 启动填充 */
    HAL_DMA2D_Start(&hdma2d, color,
                    (uint32_t)&framebuf[offset],  /* 目标地址 */
                    width, height);
    HAL_DMA2D_PollForTransfer(&hdma2d, HAL_MAX_DELAY);
}

/* ── 图片混合加速 ── */
void lv_draw_sw_blend_cb(lv_draw_unit_t *draw_unit,
                          const lv_draw_image_dsc_t *dsc,
                          const lv_area_t *coords)
{
    /* 配置 DMA2D 内存到内存混合模式 */
    hdma2d.Init.Mode = DMA2D_M2M_BLEND;  /* FG + BG 像素混合 */
    /* ... 具体实现略，根据 LVGL 版本调整 */
}
```

> LVGL v9 的 DMA2D 集成方式与 v8 不同，需要通过 `lv_draw_sw_init()` 的 `evaluate_cb` 注册。

### 6.2 硬件加速效果

| 操作 | 软件渲染 | DMA2D 加速 | 提升 |
|------|----------|------------|------|
| 全屏纯色填充 | ~15 FPS | ~45 FPS | 3x |
| 按钮 + 文字混合 | ~10 FPS | ~35 FPS | 3.5x |
| 图片缩放/旋转 | ~5 FPS | ~25 FPS | 5x |
| 透明度混合 | ~8 FPS | ~30 FPS | 3.7x |

---

## 七、裸机 main.c 完整示例

```c
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

int main(void)
{
    /* 1. HAL 初始化 */
    HAL_Init();
    SystemClock_Config();

    /* 2. 外设初始化 */
    MX_GPIO_Init();
    MX_SPI2_Init();         /* 屏幕 SPI */
    MX_DMA_Init();

    /* 3. 屏幕硬件初始化 */
    lcd_init();             /* 复位、配置寄存器、开背光 */

    /* 4. LVGL 初始化 */
    lv_init();

    /* 5. 注册驱动 */
    lv_port_disp_init();    /* 显示驱动 */
    lv_port_indev_init();   /* 触摸驱动 */

    /* 6. 创建 UI */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello LVGL!");
    lv_obj_center(label);

    lv_obj_t *btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me");
    lv_obj_center(btn_label);

    /* 7. 事件循环 */
    while (1) {
        lv_timer_handler();  /* ★ LVGL 心跳：处理 UI 事件和刷新 */
        HAL_Delay(5);        /* 5ms 间隔，约 200 FPS 的 LVGL 内部时钟 */
    }
}
```

### FreeRTOS 版本

```c
void lvgl_task(void *pvParameters)
{
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

int main(void)
{
    /* ...硬件初始化同上... */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);

    vTaskStartScheduler();
    while (1) {}
}
```

---

## 八、移植验证

### 8.1 编译检查

```c
/* main.c 中添加头文件，确认编译通过 */
#include "lvgl.h"

void lvgl_smoke_test(void)
{
    lv_init();

    printf("[LVGL] Version: %d.%d.%d\n",
           LVGL_VERSION_MAJOR,
           LVGL_VERSION_MINOR,
           LVGL_VERSION_PATCH);

    printf("[LVGL] Color depth: %d bit\n", LV_COLOR_DEPTH);
    printf("[LVGL] Memory size: %lu bytes\n", (unsigned long)LV_MEM_SIZE);

    printf("[LVGL] Init OK\n");
}
```

### 8.2 功能验证清单

| 测试项 | 方法 | 预期结果 |
|--------|------|----------|
| 编译 | `lv_init()` 调用不报错 | 编译 0 Error, 0 Warning |
| 显示 | `lv_label_create()` 显示 "Hello LVGL" | 屏幕显示文字 |
| 刷新 | 创建动画（渐入按钮） | 画面流畅，无撕裂 |
| 触摸 | 创建按钮，点击有响应 | `LV_EVENT_CLICKED` 触发 |
| 帧率 | LVGL 内置 FPS 显示 | ≥ 30 FPS |
| 内存 | 查看 `.map` 文件 | LVGL 占用 Flash < 200KB, RAM < 60KB |
| 长时间 | 运行 1 小时 | 无卡顿、无内存泄漏 |

### 8.3 启用 FPS 监控

```c
/* 在 lv_conf.h 中启用 */
#define LV_USE_PERF_MONITOR     1

/* main 中启用 */
#include "lv_demo_benchmark.h"
lv_demo_benchmark();    /* 跑官方的 Benchmark 测试 */
```

---

## 九、常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 编译报 `lv_conf.h` 找不到 | Include Path 未添加 | 添加 LVGL 根目录到 Include Path |
| 编译报 `undefined reference to lv_xxx` | 源文件未加入工程 | 检查 `.c` 是否已加入编译分组 |
| 屏幕无显示 | flush_cb 未正确实现 | 检查 SPI/DMA 配置，确认 LCD 初始化顺序 |
| 屏幕花屏 | 颜色格式不匹配 | 确认 `LV_COLOR_DEPTH` 与 LCD 设置一致（通常是 16 或 32） |
| 触摸无响应 | indev 未注册 | 检查 `lv_indev_set_read_cb()` 是否调用 |
| 触摸坐标偏移 | 未做坐标映射 | 触摸坐标需 × 比例因子映射到屏幕分辨率 |
| 界面卡顿 | 缓冲区太小 | 增大 `buf1/buf2`，或改用双缓冲 |
| `lv_timer_handler` 耗时太长 | 渲染区域过大 | 检查 `lv_conf.h` 中 `LV_DRAW_BUF_SIZE` 设置 |
| Flash 溢出 | 启用了太多控件/字体 | 裁剪 `lv_conf.h`：关闭不需要的控件和字号 |
| HardFault | 缓冲区对齐问题或栈溢出 | 用 `__attribute__((aligned(64)))` 对齐缓冲区；增大任务栈 |
| HAL_Delay 与 lv_timer_handler 冲突 | SysTick 中断优先级 | 确保 SysTick 优先级高于 SPI/DMA 中断 |
| PNG/GIF 解码失败 | 堆内存不足 | PNG 解码需要额外 32KB 以上堆空间 |

---

## 十、与 CubeMX / TouchGFX 的对比

| 对比项 | LVGL 源码移植 | CubeMX TouchGFX | TouchGFX Designer |
|--------|---------------|-----------------|-------------------|
| **难度** | 中等（需写驱动适配层） | 低（图形化配置） | 低（拖拽式 UI） |
| **版本** | 任意版本，紧跟 GitHub | 绑定 CubeMX 版本 | 绑定 TouchGFX 版本 |
| **裁剪** | 完全控制 | 有限选项 | 自动优化 |
| **控件丰富度** | 30+（开源社区活跃） | 20+（ST 官方组件） | 20+ |
| **跨平台** | 任何 MCU | 仅 STM32 | 仅 STM32 |
| **RAM 开销** | 小（8KB 起） | 较大（需帧缓冲） | 较大 |
| **动画能力** | 60 FPS 动画系统 | 支持但配置复杂 | 可视化编辑 |
| **字体支持** | TTF/OTF + 内置字体 | 仅内置字体 | TrueType 支持有限 |
| **社区生态** | GitHub 17K+ Star，文档完善 | ST 官方技术支持 | ST 社区 |
| **商用许可** | MIT（免费商用） | STM32 授权（免费） | STM32 授权（免费） |
| **学习价值** | 高（理解驱动层） | 中 | 低（黑盒） |

---

## 十一、参考资源

- [LVGL GitHub 仓库](https://github.com/lvgl/lvgl)
- [LVGL 官方文档 (v9)](https://docs.lvgl.io/master/)
- [LVGL 论坛](https://forum.lvgl.io/)
- [[LVGL 学习索引]] — 全部 LVGL 笔记导航
- [[环境配置|LVGL 环境配置]] — lv_conf.h 详解 + CMake 配置
- [[显示驱动移植|LVGL 显示驱动移植]] — flush_cb 详解 + 缓冲区策略
- [[输入设备驱动|LVGL 输入设备驱动]] — 触摸/编码器/按键适配
- [[运行逻辑|LVGL 运行逻辑]] — lv_timer_handler 内部机制
- [[LVGL 架构总览|LVGL 架构总览]] — 整体架构理解
- [[创建新文件方便后续移植]] — 模块化工程结构

---

## 十二、跨平台移植：ESP32（ESP-IDF）实战备注

本文档主体以 STM32 为主线，本节补充 ESP32 平台的适配差异和实战踩坑经验。

> **实际移植目标**：ESP32-S3 + ESP-IDF v5.5.1 + LVGL v9.5 + ILI9341（SPI）+ XPT2046 触摸屏
> 
> **移植文件**：`lv_port_disp.c`、`lv_port_indev.c`、`lv_port_tick.c`、`touch_driver.c/h`

### 12.1 移植文件清单与职责（ESP32 版）

| 文件 | 职责 | 状态 |
|------|------|------|
| `lv_port_disp.c` | SPI 初始化 + ILI9341 初始化 + flush 回调 | 需从 STM32 版重写 |
| `lv_port_indev.c` | XPT2046 触摸读取回调 + 坐标校准 | 需修正 API + 补齐驱动 |
| `lv_port_tick.c` | 用 esp_timer 提供 1ms 心跳 | 正确 |
| `touch_driver.h/.c` | XPT2046 SPI 驱动层（新建） | 需创建 |

### 12.2 时钟源差异：esp_timer 替代 SysTick

ESP32 **没有 SysTick**，必须使用 ESP-IDF 提供的定时器替代方案：

| STM32 方式 | ESP32 等价方式 | 评价 |
|---|---|---|
| SysTick_Handler 中断 | — | ❌ ESP32 无 SysTick |
| FreeRTOS 软件定时器 | `xTimerCreate()` | ⚠️ 可用，精度不如 esp_timer |
| **esp_timer（推荐）** | `esp_timer_create()` + `esp_timer_start_periodic()` | ✅ 64-bit 硬件定时器，us 级精度 |

```c
/* ESP32 版 lv_port_tick.c */
#include "esp_timer.h"
#include "lvgl.h"

static void lv_tick_cb(void *arg)
{
    lv_tick_inc(1);  /* 每 1ms 递增 LVGL 内部滴答 */
}

void lv_port_tick_init(void)
{
    esp_timer_handle_t tick_timer = NULL;
    const esp_timer_create_args_t timer_args = {
        .callback = &lv_tick_cb,
        .name = "lv_tick"
    };
    esp_timer_create(&timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 1000);  /* 1000 us = 1 ms */
}
```

### 12.3 LVGL v9.5 API 变化（vs v9.2）

- `lv_indev_create()` 不再接受参数——v9.5 会直接在无参版本上报错，不能延续 v9.2 传参写法
- `lv_display_flush_ready()` **必须在 flush 回调中调用**——STM32 版常写在 `HAL_SPI_TxCpltCallback`（DMA 中断），ESP32 polling 模式下直接在 flush 末尾调用

### 12.4 CMakeLists.txt 配置要点（ESP-IDF）

- `SRCS` 块必须连续，新文件不能夹在 `INCLUDE_DIRS` 和 `REQUIRES` 之间
- `REQUIRES` 必须包含 `driver`、`app_config`、`esp_timer`、`freertos`
- v9.5 字体路径从 `src/fonts/` 改为 `src/font/`

### 12.5 引脚限制：ESP32 vs ESP32-S3

ESP32-S3 的 **GPIO 22~25 不存在**（保留给内部 Flash/PSRAM）。直接将 STM32 或普通 ESP32 的引脚配置表复制到 S3 项目会造成 GPIO 初始化失败。

### 12.6 XPT2046 触摸驱动要点

- SPI 模式 0，最大时钟 ≤ 2 MHz
- 命令字 `0x90`（X）/ `0xD0`（Y），12-bit 差分模式
- 按下判断：原始值 200~4000 为有效触摸范围
- 触摸与显示共享 SPI 总线时，初始化顺序：**显示 → 触摸 → indev → tick**

### 12.7 非 LVGL 阻塞问题

| 问题 | 文件 | 修复 |
|------|------|------|
| UTF-8 BOM 导致 kconfgen 崩溃 | `sdkconfig` | 去掉文件开头的 BOM |
| `ipaddr_addr` 隐式声明 | `bsp_uart.c` | `#include "lwip/inet.h"` |
| `soil_sensor_get_data` 不存在 | `main.c:189` | 改为 `soil_sensor_read_data` |

### 12.8 源码不完整的典型症状

> 如果看到以下错误，说明 LVGL 源树不完整，不要手动拼凑。

| 现象 | 缺失内容 |
|------|----------|
| `lv_draw_label_private.h: No such file` | `src/draw/` 下缺少全部 `_private.h` |
| `lv_conf_internal.h: No such file` | LVGL 内部配置生成文件 |
| `../misc/lv_types.h: No such file` | 整个 `src/misc/` 目录缺失 |

**结论**：不要手动拼凑 LVGL 源文件，应直接下载完整版本再用 `lv_conf.h` 裁剪。

### 12.9 移植快速检查清单

- [ ] `lv_conf.h` 可被 `#include` 找到
- [ ] `CMakeLists.txt` 中 `SRCS` 包含所有移植文件
- [ ] `REQUIRES` 包含 `driver` / `app_config` / `esp_timer` / `freertos`
- [ ] `lv_port_tick.c` 使用 `esp_timer`（非 SysTick）
- [ ] `lv_port_disp.c` 中 `lv_display_flush_ready()` 被正确调用
- [ ] `lv_port_indev.c` 中 `lv_indev_create()` 无参数（v9.5）
- [ ] 引脚定义适配目标芯片（S3 无 GPIO 22~25）
- [ ] 触摸初始化在显示初始化之后