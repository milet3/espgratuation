# Read original and get the full Studio_OTA_CheckTask implementation
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()

orig_lines = orig_data.decode("utf-8", errors="replace").split("\n")

# The real Studio_OTA_CheckTask is at 1772 onwards (1-indexed). 
# It's a big function. Let me extract from 1772 to the closing brace.
# Looking at the structure, this function likely ends before WiFi_Cat1_StartOTA around line 1960
# Actually it goes from 1772 to ~1954 (before #if 0 WiFi_Cat1_PropertyVersion)
# Let me be safe and take 1772 to 1960

studio_ota_body = orig_lines[1771:1960]  # 0-indexed: line 1772 to 1960

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    current = f.read()

current_text = current.decode("utf-8", errors="replace")
lines = current_text.split("\n")

# Find the #if 0 / void Studio_OTA_CheckTask / #endif block and replace it
new_lines = []
i = 0
while i < len(lines):
    if lines[i].strip() == "#if 0" and i+2 < len(lines) and "Studio_OTA_CheckTask" in lines[i+1] and lines[i+2].strip() == "#endif":
        # Skip these 3 lines and insert the real function
        new_lines.extend(studio_ota_body)
        i += 3
        print(f"Replaced #if 0 block with Studio_OTA_CheckTask implementation ({len(studio_ota_body)} lines)")
        continue
    new_lines.append(lines[i])
    i += 1

# Now find OneNET_FuseOTA_CheckTask and WiFi_Cat1_CheckOTATask - they call Studio_OTA_CheckTask
# They're fine now since the definition comes before them (the OTA notify stubs are after Studio_OTA_CheckTask)

# Remove duplicate WiFi_Cat1_StartOTA that was appended from original
# Look for the SECOND occurrence
first_start_ota = -1
second_start_ota = -1
for i, line in enumerate(new_lines):
    if "void WiFi_Cat1_StartOTA" in line:
        if first_start_ota == -1:
            first_start_ota = i
        else:
            second_start_ota = i
            break

if second_start_ota > 0:
    # Remove from second occurrence to end of function (find matching })
    brace_count = 0
    end_idx = second_start_ota
    for j in range(second_start_ota, len(new_lines)):
        brace_count += new_lines[j].count("{") - new_lines[j].count("}")
        if brace_count == 0 and "}" in new_lines[j]:
            end_idx = j
            break
    new_lines = new_lines[:second_start_ota] + new_lines[end_idx+1:]
    print(f"Removed duplicate WiFi_Cat1_StartOTA (lines {second_start_ota+1} to {end_idx+1})")

result = "\n".join(new_lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print("Done")
