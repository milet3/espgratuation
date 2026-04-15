#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_STARTED
} wifi_state_t;

typedef void (*p_wifi_state_callback)(wifi_state_t state);

void wifi_manager_init(void);

esp_err_t wifi_manager_connect(const char* ssid, const char* password);

// AP配网相关函数
esp_err_t wifi_manager_start_ap(const char* ssid, const char* password);
esp_err_t wifi_manager_start_ap_provisioning(const char* ap_ssid, const char* ap_password);

void wifi_set_state_callback(p_wifi_state_callback cb);

#endif
