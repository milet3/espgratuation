with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

import re

# Fix unused function/variable warnings
unused_patterns = [
    "static const char *json_string_or_fallback",
    "static bool looks_like_base64_secret",
    "static const char *copy_json_string_or_number",
    "static bool looks_like_decimal_string",
    "static bool ota_url_is_https",
    "static const char *gateway_firmware_version",
    "static const char *const g_ota_devinfo_url_templates",
]

for i, line in enumerate(lines):
    for pattern in unused_patterns:
        if pattern in line and "__attribute__" not in line:
            lines[i] = line.replace(pattern, "__attribute__((unused)) " + pattern)
            print(f"Added unused: {pattern[:50]} at line {i+1}")
            break

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print("Applied unused attributes")
