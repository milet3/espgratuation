with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()

text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Find and remove the broken duplicate WiFi_Cat1_StartOTA (second occurrence)
first_idx = -1
second_idx = -1
for i, line in enumerate(lines):
    if "void WiFi_Cat1_StartOTA" in line:
        if first_idx == -1:
            first_idx = i
        else:
            second_idx = i
            break

if second_idx > 0:
    lines = lines[:second_idx]
    print(f"Removed duplicate WiFi_Cat1_StartOTA from line {second_idx+1} to end ({len(lines)} lines now)")

# Fix remaining garbled strings
garbled_fixes = {
    'ESP_LOGW(TAG, "\u93d2\u2020\u2014\u2020 \u00a8\u00a8\u00a8 OTA \u00e2\u2014\u0152\u00e2\u20ac\u00b0\u00e2\u20ac\u0160\u00e2\u20ac\u00a1\u00e2\u20ac\u0160\u00e2\u20ac\u0160\u00e2\u20ac\u00a1\u00e2\u20ac\u00a0\u00e2\u20ac\u00a1\u00e2\u20ac\u00a0\u00e2\u20ac\u0160\u00e2\u20ac\u00a1\u00e2\u20ac\u0160\u00e2\u20ac\u00a1\u00e2\u20ac\u0160\u00e2\u20ac\u00a1 %s",':
        '        ESP_LOGW(TAG, "Failed to mark OTA image valid: %s",',

    '             "\u00e5\u00be\u2026\u00e7\u00a1\u00ae\u00e8\u00ae\u00a4?OTA \u00e7\u203a\u00ae\u00e6\u00a0\u2021\u00e7\u2030\u02c6\u00e6\u0153\u00ac=%s\u00ef\u00bc\u0152\u00e4\u00bd\u2020\u00e5\u00bd\u201c\u00e5\u2030\u008d firmware_version=%s\u00ef\u00bc\u203a\u00e6\u0161\u00a8\u00e4\u00b8\u008d\u00e4\u00b8\u0160\u00e5\u00a0\u00b1?201",':
        '             "Pending OTA target=%s, current=%s, deferring 201 report",',

    '    ESP_LOGI(TAG, "\u00ae\u00b7\u00b2\u00e4\u00b8\u0160\u00e5\u00a0\u00b1?OTA \u00e5\u008d\u2021\u00e7\u00ba\u00a7\u00e6\u00a6\u00b4\u00e5\u0160\u0179\u00ef\u00bc\u0152firmware_version=%s",':
        '    ESP_LOGI(TAG, "Reported OTA upgrade success, firmware_version=%s",',

    '    ESP_LOGW(TAG, "\u00e4\u00b8\u0160\u00e5\u00a0\u00b1 OTA \u00e5\u008d\u2021\u00e7\u00ba\u00a7\u00e6\u00a6\u00b4\u00e5\u0160\u0179\u00e5\u00a4\u00b1\u00e8\u00b4\u00a5\u00ef\u00bc\u0152\u00e4\u00b8\u2039\u00e6\u00ac\u00a1\u00e5\u0090\u00af\u00e5\u0160\u00a8\u00e6\u2014\u00b6\u00e7\u00bb\u00a7\u00e7\u00bb\u00ad\u00e9\u2021\u008d\u00e8\u00af\u2022");':
        '    ESP_LOGW(TAG, "OTA success report failed, will retry on next boot");',
}

for i, line in enumerate(lines):
    for old, new in garbled_fixes.items():
        if old in line:
            lines[i] = new
            print(f"Fixed garbled string at line {i+1}")

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print("Done")
