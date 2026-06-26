with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()
orig_lines = orig_data.decode("utf-8", errors="replace").split("\n")

# Build a clean ota_manager.c from scratch
# Start with the functions we need in order:
# 1. OTA notify stubs (new, simple)
# 2. WiFi_Cat1_PropertyVersion (orig 1995-2033)
# 3. WiFi_Cat1_ReportBootOtaResult (orig 2035-2084) 
# 4. WiFi_Cat1_StartOTA (orig 2086-2143)
# 5. OneNET_FuseOTA_CheckTask and WiFi_Cat1_CheckOTATask wrappers

# But also keep the original ota_manager.c content (minus the broken appended parts)

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    current = f.read()
lines = current.decode("utf-8", errors="replace").split("\n")

# Find and remove everything from the OTA notify stubs to end
# (the broken appended code + the move mess)
cut_line = None
for i, line in enumerate(lines):
    if "OTA notify/reboot coordination stubs" in line:
        cut_line = i
        break

if cut_line:
    lines = lines[:cut_line]
    print(f"Cut from line {cut_line+1}, keeping {len(lines)} lines")
else:
    print("WARNING: Could not find notify stubs marker")

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print("Reverted to clean state")
