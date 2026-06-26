with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()

text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Fix any remaining lines with garbled characters in ESP_LOG calls
fixed = 0
for i, line in enumerate(lines):
    if ("ESP_LOG" in line or '             "' in line) and any(ord(c) > 127 for c in line):
        # Line contains garbled text in a log call
        if "WiFi_Cat1_ReportBootOtaResult" in "\n".join(lines[max(0,i-20):i]):
            # Inside WiFi_Cat1_ReportBootOtaResult function
            if "mark OTA" in line.lower() or "\u93d2" in line or "\u00a8" in line:
                # Failed to mark OTA image valid
                if 'ESP_LOGW' in line:
                    lines[i] = '        ESP_LOGW(TAG, "Failed to mark OTA image valid: %s",'
                    fixed += 1
                elif 'esp_err_to_name' in line:
                    pass  # keep this line
                continue
            
            if "pending" in line or "defer" in line or "\u00e5\u00be" in line or "201" in line or "target" in line:
                if "Pending" not in line and "target" in line:
                    lines[i] = '             "Pending OTA target=%s, current=%s, deferring 201 report",'
                    fixed += 1
                continue
            
            if "Reported" in line or "OTA upgrade" in line or "firmware_version=%s" in line or "\u00ae" in line:
                if "Reported OTA" not in line:
                    lines[i] = '    ESP_LOGI(TAG, "Reported OTA upgrade success, firmware_version=%s",'
                    fixed += 1
                continue
            
            if "success report failed" in line or "retry" in line or "\u00a0\u00b1" in line:
                if "OTA success" not in line:
                    lines[i] = '    ESP_LOGW(TAG, "OTA success report failed, will retry on next boot");'
                    fixed += 1
                continue

print(f"Fixed {fixed} garbled lines")

# Also check for any remaining non-ASCII in ESP_LOG contexts
for i, line in enumerate(lines):
    stripped = line.strip()
    if stripped.startswith('ESP_LOG') and any(ord(c) > 127 for c in line):
        print(f"  WARNING: Still garbled at line {i+1}: {stripped[:80]}")

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))
