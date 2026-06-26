with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Fix all remaining garbled Chinese strings by scanning for non-ASCII in ESP_LOG lines
import re
fixed = 0
for i, line in enumerate(lines):
    if re.search(r'[^\x00-\x7F]', line) and ('ESP_LOG' in line or '             "' in line or '               "' in line):
        # Replace garbled line based on context
        prev = lines[i-1] if i > 0 else ""
        next_line = lines[i+1] if i+1 < len(lines) else ""
        context = prev + line + next_line
        
        # Pattern-based replacements based on keywords in English
        if "fuse-ota" in context.lower() and "task" in context.lower() and "product" in context.lower():
            if "version=%s" in line or "version" in line:
                lines[i] = '               "version=%s (no pending upgrade, "'
                fixed += 1
            elif "task was" in line or "closed" in line:
                lines[i] = '               "or task was closed)",'
                fixed += 1
            elif "type mismatch" in line or "alternate" in line:
                lines[i] = '               "fuse-ota type mismatch, trying alternate type",'
                fixed += 1
            continue
        
        if "ESP_LOGW" in prev and "type=%d" in context:
            # Continuation of ESP_LOGW for fuse-ota type mismatch
            if "trying" in context or "alternate" in context:
                lines[i] = '               "fuse-ota type mismatch, trying alternate type",'
                fixed += 1
            continue
        
        if "ESP_LOGI" in prev and "fuse-ota" in prev and ("product=%s" in prev or "device=%s" in prev):
            lines[i] = '               "version=%s (no pending upgrade, "'
            fixed += 1
            # Check if next line also needs fixing
            if i+1 < len(lines) and re.search(r'[^\x00-\x7F]', lines[i+1]):
                lines[i+1] = '               "or task was closed)",'
                fixed += 1
            continue
                    
        if "ESP_LOG" in line and any(c > 127 for c in line):
            # Generic fix: replace garbled line
            stripped = line.rstrip().rstrip(';').rstrip(',')
            # Try to determine what this log should say
            if "fuse-ota" in context and "check" in context.lower() and "result" in context.lower():
                lines[i] = line[:line.index('"')] + '"All fuse-ota checks returned no result");'
                fixed += 1
            elif "OTA success" in context or "upgrade success" in context:
                lines[i] = line[:line.index('"')] + '"Reported OTA success, fw=%s",'
                fixed += 1
            elif "OTA report failed" in context or "retry" in context:
                lines[i] = line[:line.index('"')] + '"OTA report failed, will retry");'
                fixed += 1
            elif "Reboot success" in context or "mark" in context:
                lines[i] = line[:line.index('"')] + '"Reboot success, marked OTA image valid");'
                fixed += 1
            elif "Failed to mark" in context:
                lines[i] = line[:line.index('"')] + '"Failed to mark OTA image valid: %s",'
                fixed += 1

print(f"Fixed {fixed} garbled lines")

# Fix unused function/variable warnings
unused_patterns = [
    "static const char *json_string_or_fallback",
    "static bool looks_like_base64_secret",
    "static const char *copy_json_string_or_number",
    "static bool looks_like_decimal_string",
    "static bool ota_url_is_https",
    "static const char *gateway_firmware_version",
]
unused_vars = [
    "static const char *const g_ota_devinfo_url_templates",
]

for i, line in enumerate(lines):
    for pattern in unused_patterns:
        if pattern in line and "__attribute__" not in line:
            lines[i] = line.replace(pattern, "__attribute__((unused)) " + pattern)
            print(f"Added unused to: {pattern} at line {i+1}")
            break
    for pattern in unused_vars:
        if pattern in line and "__attribute__" not in line:
            lines[i] = line.replace(pattern, "__attribute__((unused)) " + pattern)
            print(f"Added unused to var: {pattern} at line {i+1}")
            break

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print("All fixes applied")
