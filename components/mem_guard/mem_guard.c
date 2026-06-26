/**
 * @file mem_guard.c
 * @brief Implementation of low-memory emergency degradation manager.
 */

#include "mem_guard.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MEM_GUARD";

/* ── Internal state ── */
static mem_guard_level_t s_current_level = MEM_LEVEL_NORMAL;
static uint32_t s_enter_emergency_tick = 0;
static bool s_initialised = false;

/* Forward declaration of the monitor task */
static void mem_guard_task(void *pvParameters);

/* ── Degradation-level name strings ── */
static const char *level_name(mem_guard_level_t level) {
    switch (level) {
        case MEM_LEVEL_NORMAL:    return "NORMAL";
        case MEM_LEVEL_WARN:      return "WARN";
        case MEM_LEVEL_CRITICAL:  return "CRITICAL";
        case MEM_LEVEL_EMERGENCY: return "EMERGENCY";
        default:                  return "UNKNOWN";
    }
}

/**
 * @brief Compute the "clear" (recovery) threshold for a given level.
 *
 * Once we enter a degradation level L, we only exit when free heap
 * rises above (enter_threshold + MEM_GUARD_CLEAR_MARGIN).
 */
static uint32_t clear_threshold(mem_guard_level_t level) {
    switch (level) {
        case MEM_LEVEL_WARN:
            return MEM_GUARD_WARN_THRESHOLD + MEM_GUARD_CLEAR_MARGIN;
        case MEM_LEVEL_CRITICAL:
            return MEM_GUARD_CRITICAL_THRESHOLD + MEM_GUARD_CLEAR_MARGIN;
        case MEM_LEVEL_EMERGENCY:
            return MEM_GUARD_EMERGENCY_THRESHOLD + MEM_GUARD_CLEAR_MARGIN;
        default:
            return 0;
    }
}

/**
 * @brief Determine the degradation level for a given free-heap value,
 *        with hysteresis compared to the current level.
 */
static mem_guard_level_t evaluate_level(uint32_t free_heap, mem_guard_level_t current) {
    /* Entry: check from most severe to least */
    if (free_heap <= MEM_GUARD_EMERGENCY_THRESHOLD) {
        return MEM_LEVEL_EMERGENCY;
    }
    if (free_heap <= MEM_GUARD_CRITICAL_THRESHOLD) {
        return MEM_LEVEL_CRITICAL;
    }
    if (free_heap <= MEM_GUARD_WARN_THRESHOLD) {
        return MEM_LEVEL_WARN;
    }

    /* Exit (recovery): must exceed the clear threshold for the current level */
    if (current == MEM_LEVEL_WARN && free_heap >= clear_threshold(MEM_LEVEL_WARN)) {
        return MEM_LEVEL_NORMAL;
    }
    if (current == MEM_LEVEL_CRITICAL) {
        if (free_heap >= clear_threshold(MEM_LEVEL_CRITICAL)) {
            /* Check if we should drop to WARN or NORMAL */
            if (free_heap >= clear_threshold(MEM_LEVEL_WARN)) {
                return MEM_LEVEL_NORMAL;
            }
            return MEM_LEVEL_WARN;
        }
    }
    if (current == MEM_LEVEL_EMERGENCY) {
        if (free_heap >= clear_threshold(MEM_LEVEL_EMERGENCY)) {
            if (free_heap >= clear_threshold(MEM_LEVEL_CRITICAL)) {
                if (free_heap >= clear_threshold(MEM_LEVEL_WARN)) {
                    return MEM_LEVEL_NORMAL;
                }
                return MEM_LEVEL_WARN;
            }
            return MEM_LEVEL_CRITICAL;
        }
    }

    return current; /* no change */
}

/* ── Degradation actions ── */

static void on_enter_level(mem_guard_level_t level) {
    switch (level) {
        case MEM_LEVEL_WARN:
            ESP_LOGW(TAG, "Entering WARN level - suppressing non-critical logs");
            break;

        case MEM_LEVEL_CRITICAL:
            ESP_LOGE(TAG, "Entering CRITICAL level - pausing LVGL, blocking OTA & MQTT subs");
            /* Clear the LVGL task semaphore / flag to pause refresh */
            break;

        case MEM_LEVEL_EMERGENCY:
            ESP_LOGE(TAG, "EMERGENCY: heap critically low! Preparing ordered restart...");
            s_enter_emergency_tick = xTaskGetTickCount();
            break;

        default:
            break;
    }
}

static void on_exit_level(mem_guard_level_t level) {
    ESP_LOGI(TAG, "Recovered from level %s", level_name(level));
}

/* ── Periodic monitor task ── */

static void mem_guard_task(void *pvParameters) {
    (void)pvParameters;

    while (1) {
        mem_guard_check_now();

        /* If in emergency, count down to ordered restart */
        if (s_current_level == MEM_LEVEL_EMERGENCY) {
            TickType_t elapsed = xTaskGetTickCount() - s_enter_emergency_tick;
            if (elapsed >= pdMS_TO_TICKS(MEM_GUARD_EMERGENCY_REBOOT_DELAY_MS)) {
                ESP_LOGE(TAG, "Emergency timeout reached - performing ordered restart!");
                /* Flush logs */
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MEM_GUARD_POLL_PERIOD_MS));
    }
}

/* ── Public API ── */

void mem_guard_init(void) {
    if (s_initialised) return;

    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_current_level = evaluate_level(free_heap, MEM_LEVEL_NORMAL);

    ESP_LOGI(TAG, "Memory guard initialised. Free DRAM: %" PRIu32 " bytes, level: %s",
             free_heap, level_name(s_current_level));

    BaseType_t ret = xTaskCreate(mem_guard_task, "mem_guard", 2048, NULL, 7, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mem_guard task!");
    }

    s_initialised = true;
}

mem_guard_level_t mem_guard_get_level(void) {
    return s_current_level;
}

bool mem_guard_level_at_least(mem_guard_level_t level) {
    return s_current_level >= level;
}

void mem_guard_check_now(void) {
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    mem_guard_level_t new_level = evaluate_level(free_heap, s_current_level);

    if (new_level != s_current_level) {
        if (new_level > s_current_level) {
            /* Escalating */
            ESP_LOGW(TAG, "Heap free=%" PRIu32 " min=%" PRIu32 " -> %s (was %s)",
                     free_heap, min_free, level_name(new_level), level_name(s_current_level));
            on_enter_level(new_level);
        } else {
            /* Recovering */
            ESP_LOGI(TAG, "Heap free=%" PRIu32 " min=%" PRIu32 " -> %s (was %s)",
                     free_heap, min_free, level_name(new_level), level_name(s_current_level));
            on_exit_level(s_current_level);
        }
        s_current_level = new_level;
    }
}

/* ── Safe malloc wrapper ── */

void *mem_guard_malloc(size_t size, const char *label) {
    /* Pre-check: refuse allocation in emergency */
    if (s_current_level == MEM_LEVEL_EMERGENCY) {
        ESP_LOGE(TAG, "malloc(%zu) for '%' REFUSED in EMERGENCY level", size, label);
        return NULL;
    }

    /* Check available heap */
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (size > free_heap) {
        ESP_LOGE(TAG, "malloc(%zu) for '%' would exhaust heap (free=%" PRIu32 ")",
                 size, label, free_heap);
        /* Force a check to potentially escalate level */
        mem_guard_check_now();
        return NULL;
    }

    void *ptr = malloc(size);
    if (ptr == NULL) {
        ESP_LOGE(TAG, "malloc(%zu) for '%' returned NULL (free=%" PRIu32 ")",
                 size, label, free_heap);
        /* Force a check to potentially escalate level */
        mem_guard_check_now();
    }

    return ptr;
}

void mem_guard_free(void *ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}
