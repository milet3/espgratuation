#include "crash_report.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_core_dump.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "CRASH_RPT";

/* NVS 命名空间与键名 */
#define CRASH_NVS_NS      "crash_rpt"
#define KEY_CRASH_CNT     "crash_cnt"
#define KEY_CRASH_REASON  "crash_reason"
#define KEY_CRASH_TIME    "crash_time"
#define KEY_CRASH_PC      "crash_pc"
#define KEY_CRASH_PENDING "crash_pending"

/* 复位原因 → 可读字符串映射 */
static const char *reset_reason_to_str(uint8_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT_PIN";
        case ESP_RST_SW:        return "SW_RESET";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_JTAG:      return "JTAG";
        default:                return "UNKNOWN";
    }
}

/* 判断复位原因是否为异常崩溃 */
static bool is_crash_reason(uint8_t reason) {
    switch (reason) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

void crash_report_init(void) {
    uint8_t reset_reason = (uint8_t)esp_reset_reason();

    ESP_LOGI(TAG, "Boot reset reason: %d (%s)", (int)reset_reason,
             reset_reason_to_str(reset_reason));

    if (!is_crash_reason(reset_reason)) {
        /* 正常启动，没有崩溃 */
        return;
    }

    /* --- 是异常复位，记录崩溃信息 --- */
    ESP_LOGW(TAG, "Abnormal reset detected! Recording crash info...");

    /* 1. 读取当前崩溃计数，+1 */
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CRASH_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open crash NVS: %s", esp_err_to_name(err));
        return;
    }

    uint32_t crash_cnt = 0;
    nvs_get_u32(handle, KEY_CRASH_CNT, &crash_cnt);
    crash_cnt++;
    nvs_set_u32(handle, KEY_CRASH_CNT, crash_cnt);

    /* 2. 记录崩溃原因 */
    nvs_set_u8(handle, KEY_CRASH_REASON, reset_reason);

    /* 3. 记录时间戳（启动计数作为近似） */
    nvs_set_u32(handle, KEY_CRASH_TIME, crash_cnt);

    /* 4. 尝试从 coredump 分区提取 PC */
    esp_core_dump_summary_t summary;
    err = esp_core_dump_image_check();
    if (err == ESP_OK) {
        err = esp_core_dump_get_summary(&summary);
        if (err == ESP_OK) {
            uint32_t crash_pc = summary.exc_pc;
            nvs_set_u32(handle, KEY_CRASH_PC, crash_pc);
            ESP_LOGI(TAG, "Coredump PC: 0x%08" PRIx32, crash_pc);
            ESP_LOGI(TAG, "Crashed task: %s (%s)",
                     summary.exc_task[0] ? summary.exc_task : "N/A",
                     summary.exc_bt_info.depth > 0 ? "backtrace available"
                                                     : "no backtrace");
        } else {
            nvs_set_u32(handle, KEY_CRASH_PC, 0);
        }
    } else {
        nvs_set_u32(handle, KEY_CRASH_PC, 0);
    }

    /* 5. 设置待上报标志 */
    nvs_set_u8(handle, KEY_CRASH_PENDING, 1);

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGW(TAG, "Crash #%" PRIu32 " recorded. Reason: %s",
             crash_cnt, reset_reason_to_str(reset_reason));
}

bool crash_report_has_pending(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CRASH_NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    uint8_t pending = 0;
    nvs_get_u8(handle, KEY_CRASH_PENDING, &pending);
    nvs_close(handle);

    return (pending == 1);
}

const char *crash_report_get_json(void) {
    static char json_buf[384];

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CRASH_NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        snprintf(json_buf, sizeof(json_buf),
                 "{\"evt\":\"crash\",\"err\":\"nvs_open_fail\"}");
        return json_buf;
    }

    uint32_t cnt = 0;
    uint8_t reason = 0;
    uint32_t pc = 0;
    nvs_get_u32(handle, KEY_CRASH_CNT, &cnt);
    nvs_get_u8(handle, KEY_CRASH_REASON, &reason);
    nvs_get_u32(handle, KEY_CRASH_PC, &pc);
    nvs_close(handle);

    snprintf(json_buf, sizeof(json_buf),
             "{\"evt\":\"crash\",\"cnt\":%" PRIu32
             ",\"reason\":%u,\"reason_str\":\"%s\",\"pc\":\"0x%08" PRIx32 "\"}",
             cnt, (unsigned int)reason, reset_reason_to_str(reason), pc);

    return json_buf;
}

bool crash_report_has_coredump(void) {
    esp_err_t err = esp_core_dump_image_check();
    return (err == ESP_OK);
}

void crash_report_clear(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CRASH_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open crash NVS for clear: %s",
                 esp_err_to_name(err));
        return;
    }

    nvs_set_u8(handle, KEY_CRASH_PENDING, 0);
    nvs_commit(handle);
    nvs_close(handle);

    /* 清除 coredump 分区中的数据 */
    esp_core_dump_image_erase();

    ESP_LOGI(TAG, "Crash report cleared");
}

uint32_t crash_report_get_count(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CRASH_NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) return 0;

    uint32_t cnt = 0;
    nvs_get_u32(handle, KEY_CRASH_CNT, &cnt);
    nvs_close(handle);

    return cnt;
}

uint32_t crash_report_get_last_reason(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CRASH_NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) return 0;

    uint8_t reason = 0;
    nvs_get_u8(handle, KEY_CRASH_REASON, &reason);
    nvs_close(handle);

    return (uint32_t)reason;
}
