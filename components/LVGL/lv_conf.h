/* ============================================================
 * lv_conf.h ??STM32 嵌入??LVGL v9 配置
 * 适用??STM32F4/F7/H7 + 320×240 TFT + FreeRTOS
 * ??GitHub lv_conf_template.h 复制后修?? * ============================================================ */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ── 颜色配置 ── */
#define LV_COLOR_DEPTH 16 /* RGB565（最常用??*/
/* 16 = RGB565 | 32 = RGB888/XRGB8888 | 8 = 调色??| 1 = 单色 */

/* ── 默认显示分辨??── */
#define LV_DPI_DEF 130 /* PPI，用于缩放参??*/

/* ── 内存管理 ── */
#define LV_MEM_SIZE (64 * 1024) /* LVGL 自身内存??48KB */
#define LV_MEM_ADR 0x00         /* 0 = 使用 malloc，或指定绝对地址 */
#define LV_MEM_CUSTOM 1         /* 1 = PSRAM heap_caps_malloc */
#define LV_MEMCPY_MEMSET_STD 1  /* 1 = 使用标准 memcpy/memset */

/* ── PSRAM 自定义内存分配器 ── */
#if LV_MEM_CUSTOM
#define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
#define LV_MEM_CUSTOM_ALLOC(size)    heap_caps_malloc((size), MALLOC_CAP_SPIRAM)
#define LV_MEM_CUSTOM_FREE(ptr)      heap_caps_free(ptr)
#define LV_MEM_CUSTOM_REALLOC(ptr, size) heap_caps_realloc((ptr), (size), MALLOC_CAP_SPIRAM)
#endif

/* ── 操作系统集成 ── */
#define LV_USE_OS 0 /* LV_OS_FREERTOS too slow on ESP32-S3 */

/* ── 日志级别 ── */
#define LV_USE_LOG 0 /* 0 = 关闭，开发时打开 LV_LOG_LEVEL_WARN */
#if LV_USE_LOG
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#endif

/* ── 性能选项 ── */
#define LV_DRAW_BUF_STRIDE_ALIGN 1  /* 行对齐（DMA 要求??*/
#define LV_USE_DRAW_SW 1            /* 软件渲染后端 ??必须 */
#define LV_DRAW_SW_SUPPORT_RGB565 1 /* RGB565 支持 */

/* ── 控件开关（按需启用，不用的注释掉省 Flash??── */
#define LV_USE_BUTTON 1
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_ROLLER 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1
#define LV_USE_LIST 1
#define LV_USE_CHART 1
#define LV_USE_KEYBOARD 1
#define LV_USE_MSGBOX 1
#define LV_USE_SPINBOX 1
#define LV_USE_TABVIEW 1
#define LV_USE_TILEVIEW 0 /* 少用，注释掉 */
#define LV_USE_WIN 0
#define LV_USE_SPINNER 1
#define LV_USE_LED 1
#define LV_USE_SPAN 0
#define LV_USE_SCALE 0

/* ── 布局系统 ── */
#define LV_USE_FLEX 1 /* Flex 布局 */
#define LV_USE_GRID 1 /* Grid 布局 */

/* ── 主题 ── */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_MONO 0
#define LV_THEME_DEFAULT_GROW 1 /* 新控件自动继承主??*/

/* ── 字体 ── */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1 /* 14px 默认字体 */
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_48 0
/* LV_FONT_DEFAULT = &lv_font_montserrat_14 */

/* ── 图片解码器（按需启用??── */
#define LV_USE_BIN_DECODER 0 /* Binary image decoder */
#define LV_USE_BMP 0         /* BMP format */
#define LV_USE_PNG 0         /* PNG（需额外 RAM??*/
#define LV_USE_JPG 0         /* JPG */
#define LV_USE_GIF 0         /* GIF */
#define LV_USE_SJPG 0        /* Split-JPG（流式） */
#define LV_USE_FREETYPE 0    /* TTF 字体 */

/* ── 动画与特??── */
#define LV_USE_ANIM 1        /* 动画系统 */
#define LV_USE_SHADOW 0      /* 阴影（省资源时关闭） */
#define LV_USE_BLEND_MODES 0 /* 混合模式 */

/* ── 杂项 ── */
#define LV_USE_SNAPSHOT 0      /* 截图 */
#define LV_USE_FILE_EXPLORER 0 /* 文件浏览??*/
#define LV_USE_QRCODE 0        /* 二维??*/

/* ── STM32 硬件加??── */
/* F4/F7/H7 ??DMA2D（Chrom-ART??*/
#define LV_USE_DRAW_DMA2D 0 /* 1 = 启用 DMA2D 加速（需实现回调??*/

/* ── 输入设备默认分组 ── */
#define LV_INDEV_DEF_READ_PERIOD 30 /* 输入读取周期 (ms) */

/* ── 帧率 ── */
#define LV_DEF_REFR_PERIOD 16 /* 默认刷新周期 (ms)，约 30 FPS */

/* ---- Performance monitor (FPS + CPU) ---- */
#define LV_USE_PERF_MONITOR 1
#define LV_USE_SYSMON 1

#define LV_SYSMON_GET_IDLE lv_sysmon_get_idle_stub

#endif /* LV_CONF_H */
