with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()

# Decode with errors='replace' to turn garbled bytes into �
text = data.decode("utf-8", errors="replace")

lines = text.split("\n")

fixes = {
    313: '        ESP_LOGI(TAG, "No executable OTA task for this firmware version");',
    753: '        ESP_LOGI(TAG,',
    754: '                 "No matching fuse-ota task: product=%s, device=%s, type=%d, "',
    755: '                 "version=%s (no pending upgrade for this device/version, "',
    756: '                 "or previous task was closed)",',
    776: '      ESP_LOGI(TAG, "No executable OTA task for this firmware version");',
    1295: '        ESP_LOGI(TAG, "OTA chunk %d: %d bytes, total %u/%d",',
    1408: '    ESP_LOGI(TAG, "Requesting Studio OTA endpoint: %s", url);',
    1435: '            ESP_LOGI(TAG, "Studio OTA response: %s", buffer);',
    1445: '                  ESP_LOGI(TAG, "Got firmware download URL: %s", download_url);',
    1497: '    ESP_LOGI(TAG, "Checking OTA tasks, firmware_version=%s",',
    1563: '        ESP_LOGW(TAG,',
    1564: '                 "fuse-ota query type=%d not matched, trying type=%d",',
    1573: '      ESP_LOGW(TAG, "All fuse-ota checks returned no usable result");',
    1678: '      ESP_LOGI(TAG, "WiFi mode version report: %s", tempdatabuff);',
    1692: '      ESP_LOGW(TAG, "MQTT not connected, skip firmware_version report");',
    1715: '        ESP_LOGI(TAG, "Reported firmware_version=%s",',
    1718: '        ESP_LOGE(TAG, "Failed to report firmware_version");',
    1733: '      ESP_LOGW(TAG, "MQTT token empty, skip OTA success report");',
    1760: '      ESP_LOGW(TAG,',
    1761: '               "Pending OTA target=%s, current=%s, deferring 201 report",',
    1769: '      ESP_LOGI(TAG, "Reported OTA upgrade success, firmware_version=%s",',
    1773: '      ESP_LOGW(TAG, "OTA success report failed, will retry on next boot");',
    1780: '      ESP_LOGE(TAG, "OTA download URL is empty");',
    1785: '      ESP_LOGW(TAG, "ESP-IDF OTA does not support sub-device OTA");',
    1790: '      ESP_LOGW(TAG, "OTA already in progress, ignoring duplicate");',
    1799: '    ESP_LOGI(TAG, "Starting gateway OTA download: %s", url);',
    1816: '      ESP_LOGE(TAG, "Gateway OTA failed: %s", esp_err_to_name(err));',
    1831: '    ESP_LOGI(TAG, "Gateway OTA complete, rebooting into new firmware...");',
}

for lineno, new_content in fixes.items():
    idx = lineno - 1
    if idx < len(lines):
        lines[idx] = new_content

result = "\n".join(lines)

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print(f"Fixed {len(fixes)} lines")
