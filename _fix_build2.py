with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()

text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Fix 1: Add esp_mqtt.h include after existing includes (before static const char *TAG)
for i, line in enumerate(lines):
    if line.strip() == 'static const char *TAG = "OTA";':
        lines.insert(i, '#include "esp_mqtt.h"')
        print(f"Added esp_mqtt.h include before line {i+1}")
        break

# Fix 2: Find #if 0 without matching #endif around line 1398
for i in range(1390, 1420):
    if i < len(lines) and lines[i].strip() == "#if 0":
        # Find the matching function or end
        for j in range(i+1, min(i+500, len(lines))):
            if lines[j].strip().startswith("#endif"):
                print(f"Found matching #endif at line {j+1}")
                break
        else:
            # No #endif found, need to add one
            # Find a good place - end of function or end of block
            print(f"#if 0 at line {i+1} has no matching #endif, searching for insertion point...")
            # Just close it after a few lines or at the next standalone function
            for j in range(i+1, min(i+100, len(lines))):
                if lines[j].strip() == "" and j+1 < len(lines) and lines[j+1].strip().startswith("static "):
                    lines.insert(j+1, "#endif")
                    print(f"Added #endif after line {j+1}")
                    break
            else:
                lines.insert(i+10, "#endif")
                print(f"Added #endif at line {i+11} (fallback)")
        break

# Fix 3: Add __attribute__((unused)) to unused functions and variables
unused_funcs = [
    "static bool ota_pending_info_load",
    "static void ota_pending_info_clear",
    "static esp_err_t ota_report_fuse_status",
    "static esp_err_t ota_parse_fuse_check_response",
    "static void log_ota_network_snapshot",
]
unused_vars = [
    "static const char *g_fuse_ota_status_url_template",
    "static const char *g_fuse_ota_download_url_template",
    "static char g_ota_target_version[64]",
    "static char g_ota_status_url[512]",
]

for i, line in enumerate(lines):
    for fn in unused_funcs:
        if fn in line and "__attribute__" not in line:
            lines[i] = line.replace(fn, "__attribute__((unused)) " + fn)
            print(f"Added unused to: {fn} at line {i+1}")
            break
    for var in unused_vars:
        if var in line and "__attribute__" not in line:
            lines[i] = line.replace(var, "__attribute__((unused)) " + var)
            print(f"Added unused to: {var} at line {i+1}")
            break

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print("Done")
