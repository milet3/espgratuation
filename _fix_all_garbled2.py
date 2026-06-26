import re

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()

# Read as binary to avoid encoding issues
lines_b = data.split(b"\n")
fixed = 0

# Known garbled → English replacement map (as bytes)
replacements = {
    # Line 1748
    b'ESP_LOGI(TAG, "\xe9\x98\x85\xe5\xb4\x8e\xe5\x82\x8e\xe6\x84\x8e\xe5\xa4\x96\xe5\xb0\x8e\xe5\x8c\x97\xe5\xb0\x8e?OTA \xe9\x95\x8c\xe5\xbd\x9c\xe5\x8f\xa5\xe6\xa0\x82\xe8\xb2\xb0\xe8\x86\x8f\xe7\x82\xba\xe6\x9c\x92\xe5\x8c\xaf?)': b'  // fixed below',
}

for i, line in enumerate(lines_b):
    # Check for lines with non-ASCII in ESP_LOG context
    if b"ESP_LOG" in line or b'            "' in line or b'               "' in line:
        try:
            decoded = line.decode("ascii")
        except UnicodeDecodeError:
            # Has non-ASCII — check context and fix
            context_before = lines_b[i-1] if i > 0 else b""
            context_after = lines_b[i+1] if i+1 < len(lines_b) else b""
            
            if b"ESP_LOG" in line:
                # Find the string position
                idx = line.find(b'"')
                if idx >= 0:
                    prefix = line[:idx]
                    # Replace the entire log line based on context
                    if b"Reboot" in context_before or b"mark_err" in context_after or b"esp_ota_mark" in context_before:
                        if b"ESP_LOGI" in line:
                            lines_b[i] = prefix + b'"Reboot success, marked OTA image valid");'
                            fixed += 1
                        elif b"ESP_LOGW" in line:
                            lines_b[i] = prefix + b'"Failed to mark OTA image valid: %s",'
                            fixed += 1
                        elif b"ESP_LOGE" in line:
                            lines_b[i] = prefix + b'"Failed to mark OTA image: %s",'
                            fixed += 1
                    elif b"Pending" in context_before or b"OTA target" in context_before:
                        lines_b[i] = prefix + b'"Pending OTA target=%s, current=%s, deferring 201 report",'
                        fixed += 1
                    elif b"Reported" in context_before or b"OTA success" in context_before:
                        lines_b[i] = prefix + b'"Reported OTA upgrade success, firmware_version=%s",'
                        fixed += 1
                    elif b"OTA report failed" in context_before or b"will retry" in context_before:
                        lines_b[i] = prefix + b'"OTA success report failed, will retry on next boot");'
                        fixed += 1
                    elif b"fuse-ota" in line.lower():
                        lines_b[i] = prefix + b'"fuse-ota type mismatch, trying alternate type",'
                        fixed += 1
                    elif b"version=%s" in line or b"no pending" in line:
                        lines_b[i] = prefix + b'"version=%s (no pending upgrade, "'
                        fixed += 1
                    elif b"task was closed" in line:
                        lines_b[i] = prefix + b'"or task was closed)",'
                        fixed += 1
                    elif b"firmware_version" in line and b"ESP_LOGI" in line:
                        lines_b[i] = prefix + b'"Reported firmware_version=%s",'
                        fixed += 1
                    elif b"firmware_version" in line and b"ESP_LOGE" in line:
                        lines_b[i] = prefix + b'"Failed to report firmware_version");'
                        fixed += 1
                    elif b"MQTT" in line and b"token" in line:
                        lines_b[i] = prefix + b'"MQTT token empty, skip OTA success report");'
                        fixed += 1
                    elif b"MQTT" in line and b"not connected" in line:
                        lines_b[i] = prefix + b'"MQTT not connected, skip firmware_version report");'
                        fixed += 1
                    elif b"Gateway OTA" in line:
                        lines_b[i] = prefix + b'"Gateway OTA complete, rebooting...");'
                        fixed += 1
                    elif b"fuse-ota" in line and b"result" in line:
                        lines_b[i] = prefix + b'"All fuse-ota checks returned no usable result");'
                        fixed += 1
                    elif b"OTA chunk" in line:
                        lines_b[i] = prefix + b'"OTA chunk %d: %d bytes, total %u/%d",'
                        fixed += 1
                    else:
                        # Generic: just close the string
                        lines_b[i] = prefix + b'"OTA log message");'
                        fixed += 1
                        print(f"Generic fix at line {i+1}")
            
            elif b'"' in line and (b"version=%s" in context_before or b"product=%s" in context_before):
                # Continuation line
                if b"no pending" in line or b"upgrade" in line:
                    lines_b[i] = b'               "version=%s (no pending upgrade, "'
                    fixed += 1
                elif b"closed" in line:
                    lines_b[i] = b'               "or task was closed)",'
                    fixed += 1

result = b"\n".join(lines_b)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result)

print(f"Fixed {fixed} lines")
