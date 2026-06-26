with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()
orig_lines = orig_data.decode("utf-8", errors="replace").split("\n")

# Extract WiFi_Cat1_StartOTA: lines 2086-2143 (0-indexed 2085-2142)
start_ota = orig_lines[2085:2143]

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    current = f.read()
text = current.decode("utf-8", errors="replace")
lines = text.split("\n")

# Find and remove the broken partial WiFi_Cat1_StartOTA at the end
new_lines = []
i = 0
while i < len(lines):
    if "void WiFi_Cat1_StartOTA" in lines[i] and i > len(lines) - 30:
        # This is the broken one at the end; skip it and everything after
        break
    new_lines.append(lines[i])
    i += 1

# Append the full function
new_lines.extend(start_ota)

result = "\n".join(new_lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print(f"Replaced broken StartOTA with full version ({len(start_ota)} lines)")
print(f"Final line count: {len(new_lines)}")
