# Extract missing OTA functions from original file (lines 1995-2088)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()

orig_lines = orig_data.decode("utf-8", errors="replace").split("\n")

# Lines 1995-2033: WiFi_Cat1_PropertyVersion (0-indexed 1994-2032)
# Lines 2035-2088: WiFi_Cat1_ReportBootOtaResult (0-indexed 2034-2087)
missing = orig_lines[1994:2033] + orig_lines[2034:2088]

# Read current ota_manager.c
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    current = f.read()

current_text = current.decode("utf-8", errors="replace")

# Add OneNET_FuseOTA_CheckTask wrapper before appending missing code
wrapper = '''
void OneNET_FuseOTA_CheckTask(void) {
  Studio_OTA_CheckTask();
}

void WiFi_Cat1_CheckOTATask(uint8_t num) {
  if (num == 0) {
    Studio_OTA_CheckTask();
  }
}
'''

# Find the OTA notify stubs and add before them
if "/* ── OTA notify/reboot coordination stubs ── */" in current_text:
    current_text = current_text.replace(
        "/* ── OTA notify/reboot coordination stubs ── */",
        wrapper + "\n/* ── OTA notify/reboot coordination stubs ── */"
    )
else:
    # Append at end
    current_text = current_text.rstrip() + "\n" + wrapper

# Now append the missing original functions
missing_text = "\n".join(missing)
current_text = current_text.rstrip() + "\n" + missing_text

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(current_text.encode("utf-8"))

print(f"Added {len(missing)} lines of missing functions + wrapper")
