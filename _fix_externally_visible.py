with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")

# Remove existing __attribute__((used)) 
text = text.replace("__attribute__((used)) ", "")

# Add externally_visible (stronger than used — forces global visibility)
funcs = [
    ("void WiFi_Cat1_RequestOtaNotifyReboot(void)", '__attribute__((externally_visible)) void WiFi_Cat1_RequestOtaNotifyReboot(void)'),
    ("bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void)", '__attribute__((externally_visible)) bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void)'),
    ("void OneNET_FuseOTA_CheckTask(void)", '__attribute__((externally_visible)) void OneNET_FuseOTA_CheckTask(void)'),
    ("void WiFi_Cat1_CheckOTATask(uint8_t num)", '__attribute__((externally_visible)) void WiFi_Cat1_CheckOTATask(uint8_t num)'),
    ("void WiFi_Cat1_PropertyVersion(uint8_t num)", '__attribute__((externally_visible)) void WiFi_Cat1_PropertyVersion(uint8_t num)'),
    ("void WiFi_Cat1_ReportBootOtaResult(void)", '__attribute__((externally_visible)) void WiFi_Cat1_ReportBootOtaResult(void)'),
    ("void WiFi_Cat1_StartOTA(const char *url, const char *token,", '__attribute__((externally_visible)) void WiFi_Cat1_StartOTA(const char *url, const char *token,'),
]

for old, new in funcs:
    text = text.replace(old, new)
    if old in text:
        print(f"WARNING: Multiple occurrences of {old[:40]}")

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(text.encode("utf-8"))

print("Added externally_visible attribute")
