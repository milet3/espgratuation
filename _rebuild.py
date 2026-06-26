with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()
orig = orig_data.decode("utf-8", errors="replace")
orig_lines = orig.split("\n")

# The handoff specifies OTA extraction boundaries (0-indexed from original):
# [47..76] + [77..191] + [193..1303] + [1708..2143]
ota_sections = [
    orig_lines[46:76],   # lines 47-76
    orig_lines[76:191],  # lines 77-191
    orig_lines[192:1303], # lines 193-1303
    orig_lines[1707:2143], # lines 1708-2143
]

# Build the full OTA extracted content
ota_body = []
for section in ota_sections:
    ota_body.extend(section)
ota_text = "\n".join(ota_body)

# Now read the current ota_manager.c to get the includes, TAG, pool code, etc.
# and rebuild properly
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.h", "rb") as f:
    header = f.read().decode("utf-8", errors="replace")

# Build new ota_manager.c
new_file = '''#include "ota_manager.h"
#include "app_config.h"
#include "bsp_storage.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ctype.h>
#include "math.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "utils_hmac.h"
#include "utils_md5.h"
#include "esp_mqtt.h"

static const char *TAG = "OTA";

#define OTA_SOUTH_TYPE 3

#ifndef OTA_NUMERIC_PID
#define OTA_NUMERIC_PID ""
#endif
#ifndef OTA_DEVICE_AUTHINFO
#define OTA_DEVICE_AUTHINFO ""
#endif
#ifndef OTA_DEVICE_ID
#define OTA_DEVICE_ID ""
#endif

extern esp_err_t Cat1_AT_MqttPublish(const char *topic, const char *payload);
'''

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(new_file.encode("utf-8"))

print("Wrote base includes + macros")
print(f"Next: append OTA body ({len(ota_body)} lines)")
