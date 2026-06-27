#ifndef __CRASH_REPORT_H__
#define __CRASH_REPORT_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动时调用：检测并记录上次崩溃信息到 NVS
 *
 * 必须在 NVS 初始化后、网络任务启动前调用。
 * 如果本次启动原因是异常复位（panic/WDT/brownout/assert），
 * 则将崩溃元数据写入 NVS 并设置 crash_pending 标志。
 */
void crash_report_init(void);

/**
 * @brief 检查是否有待上报的崩溃信息
 * @return true 有待上报的崩溃，false 没有
 */
bool crash_report_has_pending(void);

/**
 * @brief 获取崩溃摘要 JSON 字符串（用于 MQTT 上报）
 *
 * 返回一个静态分配的 JSON 字符串，包含：
 * - fw: 固件版本
 * - cnt: 历史崩溃总次数
 * - reason: 复位原因码
 * - reason_str: 复位原因描述
 * - time: 上次崩溃时刻的启动次数（近似）
 *
 * @return JSON 字符串指针（静态 buffer，下次调用覆盖）
 */
const char *crash_report_get_json(void);

/**
 * @brief 检查 coredump 分区是否有有效数据
 * @return true 存在有效 coredump
 */
bool crash_report_has_coredump(void);

/**
 * @brief 清除崩溃待上报标志（上报成功后调用）
 */
void crash_report_clear(void);

/**
 * @brief 获取历史崩溃总次数
 * @return 崩溃次数
 */
uint32_t crash_report_get_count(void);

/**
 * @brief 获取上次崩溃的复位原因码
 * @return esp_reset_reason_t 值
 */
uint32_t crash_report_get_last_reason(void);

#ifdef __cplusplus
}
#endif

#endif /* __CRASH_REPORT_H__ */
