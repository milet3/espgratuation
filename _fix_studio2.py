with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()

orig_lines = orig_data.decode("utf-8", errors="replace").split("\n")
# Studio_OTA_CheckTask from line 1772 to 1960 (0-indexed: 1771 to 1959)
studio_ota_body = orig_lines[1771:1960]

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    current = f.read()

current_text = current.decode("utf-8", errors="replace")
lines = current_text.split("\n")

# Find #if 0 ... Studio_OTA_CheckTask ... #endif and replace
new_lines = []
i = 0
replaced = False
while i < len(lines):
    if (lines[i].strip() == "#if 0" 
        and i+1 < len(lines) and "Studio_OTA_CheckTask" in lines[i+1]
        and not replaced):
        # Find matching #endif
        j = i + 2
        while j < len(lines) and lines[j].strip() != "#endif":
            j += 1
        if j < len(lines):
            new_lines.extend(studio_ota_body)
            i = j + 1
            replaced = True
            print(f"Replaced lines {i-j} through {i} with Studio_OTA_CheckTask ({len(studio_ota_body)} lines)")
            continue
    new_lines.append(lines[i])
    i += 1

if not replaced:
    print("WARNING: Did not find #if 0 block!")

# Remove duplicate WiFi_Cat1_StartOTA
first = -1
second = -1
for i, line in enumerate(new_lines):
    if "void WiFi_Cat1_StartOTA" in line:
        if first == -1:
            first = i
        else:
            second = i
            break

if second > 0:
    brace = 0
    end = second
    for j in range(second, len(new_lines)):
        brace += new_lines[j].count("{") - new_lines[j].count("}")
        if brace == 0 and "}" in new_lines[j]:
            end = j
            break
    new_lines = new_lines[:second] + new_lines[end+1:]
    print(f"Removed duplicate WiFi_Cat1_StartOTA ({end - second + 1} lines)")

# Also remove the old WiFi_Cat1_PropertyVersion that might be duped  
# (the one from original 1964-1993 inside #if 0)
# Actually the appended one is the active version (1995+), the original ota_manager
# might have had it already. Let's just check for duplicates.
lines_before = len(new_lines)

result = "\n".join(new_lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print(f"Final line count: {len(new_lines)} (was {len(lines)})")
