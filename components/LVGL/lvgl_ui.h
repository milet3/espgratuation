/**
 * @file lvgl_ui.h
 * @brief LVGL v9.5 UI for agricultural IoT gateway sensor dashboard
 */
#ifndef __LVGL_UI_H__
#define __LVGL_UI_H__

#include "lvgl.h"

/**
 * @brief Create the sensor dashboard UI (tabview with 3 tabs).
 * Must be called from the LVGL task context after lv_init / display init.
 */
void lvgl_ui_create(void);

/**
 * @brief Update sensor readings on the active UI.
 * Call this periodically (e.g. every 1-2 s) from the LVGL task context.
 */
void lvgl_ui_update(float env_temp, float env_humi, float env_lux,
                    float soil_temp, float soil_humi, float soil_ec,
                    float soil_ph, float soil_n, float soil_p,
                    float soil_k, float soil_sal);

/**
 * @brief Update system status indicators on the status tab.
 */
void lvgl_ui_update_sys(bool wifi_ok, bool mqtt_ok, bool lora_ok,
                        bool ota_active, const char *ip_addr,
                        const char *fw_ver);

#endif /* __LVGL_UI_H__ */
