with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")

# Remove visibility attribute (ignored on Xtensa)
text = text.replace('__attribute__((visibility("default"))) ', '')

# Add __attribute__((used)) to force emit
funcs = [
    "void WiFi_Cat1_RequestOtaNotifyReboot(void)",
    "bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void)",
    "void OneNET_FuseOTA_CheckTask(void)",
    "void WiFi_Cat1_CheckOTATask(uint8_t num)",
    "void WiFi_Cat1_PropertyVersion(uint8_t num)",
    "void WiFi_Cat1_ReportBootOtaResult(void)",
]
for fn in funcs:
    text = text.replace(fn, f"__attribute__((used)) {fn}")

# For WiFi_Cat1_StartOTA which spans two lines
text = text.replace(
    "void WiFi_Cat1_StartOTA(const char *url, const char *token,",
    '__attribute__((used)) void WiFi_Cat1_StartOTA(const char *url, const char *token,'
)

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(text.encode("utf-8"))

print("Added __attribute__((used)) to functions")
