with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")

# Fix: replace functions with visibility("default")
text = text.replace(
    "void WiFi_Cat1_RequestOtaNotifyReboot(void) {",
    '__attribute__((visibility("default"))) void WiFi_Cat1_RequestOtaNotifyReboot(void) {'
)
text = text.replace(
    "bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void) {",
    '__attribute__((visibility("default"))) bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void) {'
)
text = text.replace(
    "void OneNET_FuseOTA_CheckTask(void) {",
    '__attribute__((visibility("default"))) void OneNET_FuseOTA_CheckTask(void) {'
)
text = text.replace(
    "void WiFi_Cat1_CheckOTATask(uint8_t num) {",
    '__attribute__((visibility("default"))) void WiFi_Cat1_CheckOTATask(uint8_t num) {'
)
text = text.replace(
    "void WiFi_Cat1_PropertyVersion(uint8_t num) {",
    '__attribute__((visibility("default"))) void WiFi_Cat1_PropertyVersion(uint8_t num) {'
)
text = text.replace(
    "void WiFi_Cat1_ReportBootOtaResult(void) {",
    '__attribute__((visibility("default"))) void WiFi_Cat1_ReportBootOtaResult(void) {'
)
text = text.replace(
    "void WiFi_Cat1_StartOTA(const char *url, const char *token,",
    '__attribute__((visibility("default"))) void WiFi_Cat1_StartOTA(const char *url, const char *token,'
)

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(text.encode("utf-8"))

print("Added visibility(default) to 7 functions")
