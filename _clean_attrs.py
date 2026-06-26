with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")

# Fix the broken visibility attributes
text = text.replace('__attribute__((used,visibility(\\"default\\")))', '__attribute__((used))')

# Now use the correct approach: remove all __attribute__((used)) and instead
# just ensure no static keyword is present.
# The real issue is these functions need to be non-static.

# Check if these functions have static
funcs_to_check = [
    "WiFi_Cat1_RequestOtaNotifyReboot",
    "WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive",
    "OneNET_FuseOTA_CheckTask",
    "WiFi_Cat1_CheckOTATask",
    "WiFi_Cat1_PropertyVersion",
    "WiFi_Cat1_ReportBootOtaResult",
    "WiFi_Cat1_StartOTA",
]

lines = text.split("\n")
for i, line in enumerate(lines):
    for fn in funcs_to_check:
        if fn + "(" in line and "static" in line and "__attribute__" not in line:
            print(f"WARNING: {fn} has static at line {i+1}")

# Remove all __attribute__ decorations from these functions
for fn in funcs_to_check:
    text = text.replace(f"__attribute__((used)) void {fn}", f"void {fn}")
    text = text.replace(f"__attribute__((used)) bool {fn}", f"bool {fn}")

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(text.encode("utf-8"))

print("Cleaned up attributes")
