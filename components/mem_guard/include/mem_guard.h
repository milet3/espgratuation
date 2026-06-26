/**
 * @file mem_guard.h
 * @brief Low-memory emergency degradation manager for ESP32
 *
 * Monitors DRAM heap and enforces a multi-level degradation policy:
 *   LEVEL 0 (NORMAL)   - All subsystems run normally
 *   LEVEL 1 (WARN)     - Non-critical logging suppressed
 *   LEVEL 2 (CRITICAL) - LVGL refresh paused, OTA rejected, new MQTT subs rejected
 *   LEVEL 3 (EMERGENCY)- Alert sent via MQTT, ordered restart triggered
 *
 * Hysteresis: recovery requires heap to rise above a higher "clear" threshold.
 */

#ifndef __MEM_GUARD_H__
#define __MEM_GUARD_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configurable thresholds (bytes of free DRAM) ── */

#ifndef MEM_GUARD_WARN_THRESHOLD
#define MEM_GUARD_WARN_THRESHOLD       (50 * 1024)   /*  50 KB */
#endif

#ifndef MEM_GUARD_CRITICAL_THRESHOLD
#define MEM_GUARD_CRITICAL_THRESHOLD   (30 * 1024)   /*  30 KB */
#endif

#ifndef MEM_GUARD_EMERGENCY_THRESHOLD
#define MEM_GUARD_EMERGENCY_THRESHOLD  (15 * 1024)   /*  15 KB */
#endif

/* Clear thresholds: must rise above these to exit a degradation level */
#ifndef MEM_GUARD_CLEAR_MARGIN
#define MEM_GUARD_CLEAR_MARGIN         (10 * 1024)   /* 10 KB hysteresis */
#endif

#ifndef MEM_GUARD_POLL_PERIOD_MS
#define MEM_GUARD_POLL_PERIOD_MS       2000           /* check every 2 s */
#endif

#ifndef MEM_GUARD_EMERGENCY_REBOOT_DELAY_MS
#define MEM_GUARD_EMERGENCY_REBOOT_DELAY_MS  3000    /* 3 s grace before restart */
#endif

/* ── Degradation levels (0 = normal, higher = more severe) ── */
typedef enum {
    MEM_LEVEL_NORMAL    = 0,
    MEM_LEVEL_WARN      = 1,
    MEM_LEVEL_CRITICAL  = 2,
    MEM_LEVEL_EMERGENCY = 3,
} mem_guard_level_t;

/* ── Public API ── */

/**
 * @brief Initialise the memory guard subsystem.
 *
 * Starts the periodic heap-monitor task.
 * Call once during app_main, after FreeRTOS scheduler is running.
 */
void mem_guard_init(void);

/**
 * @brief Get the current degradation level.
 */
mem_guard_level_t mem_guard_get_level(void);

/**
 * @brief Check whether the system is at or above a given level.
 */
bool mem_guard_level_at_least(mem_guard_level_t level);

/* ── Convenience queries for subsystem gating ── */

/** True when LVGL refresh should be paused to save memory. */
static inline bool mem_guard_lvgl_paused(void) {
    return mem_guard_level_at_least(MEM_LEVEL_CRITICAL);
}

/** True when OTA must be rejected due to low memory. */
static inline bool mem_guard_ota_blocked(void) {
    return mem_guard_level_at_least(MEM_LEVEL_CRITICAL);
}

/** True when new MQTT subscriptions must be rejected. */
static inline bool mem_guard_mqtt_sub_blocked(void) {
    return mem_guard_level_at_least(MEM_LEVEL_CRITICAL);
}

/** True when non-critical logging should be suppressed. */
static inline bool mem_guard_log_suppressed(void) {
    return mem_guard_level_at_least(MEM_LEVEL_WARN);
}

/**
 * @brief Force an immediate heap check and degradation evaluation.
 *
 * Normally called by the periodic task; may also be called before
 * large allocations to pre-emptively degrade.
 */
void mem_guard_check_now(void);

/**
 * @brief Attempt a safe malloc. Returns NULL with logging on failure.
 *
 * Pre-checks heap: if emergency level, refuses and returns NULL.
 * Wrapper around malloc() that logs the caller on failure.
 *
 * @param size  Bytes to allocate
 * @param label Human-readable label for logging (e.g. "OTA chunk", "LVGL buffer")
 * @return      Allocated pointer, or NULL
 */
void *mem_guard_malloc(size_t size, const char *label);

/**
 * @brief Free memory previously allocated by mem_guard_malloc.
 */
void mem_guard_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* __MEM_GUARD_H__ */
