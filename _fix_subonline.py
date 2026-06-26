# Fix wifi_cat1.h: change void to esp_err_t
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\wifi_cat1.h", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
text = text.replace("void WiFi_Cat1_SubOnline(char, char);", "esp_err_t WiFi_Cat1_SubOnline(char sub_num, char mode);")
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\wifi_cat1.h", "wb") as f:
    f.write(text.encode("utf-8"))
print("Fixed wifi_cat1.h")

# Fix wifi_cat1.c: change void to esp_err_t, add return ESP_OK
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\wifi_cat1.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
text = text.replace("void WiFi_Cat1_SubOnline(char sub_num, char mode) {", "esp_err_t WiFi_Cat1_SubOnline(char sub_num, char mode) {")
# Find the closing brace and add return before it
lines = text.split("\n")
in_func = False
brace_count = 0
for i, line in enumerate(lines):
    if "esp_err_t WiFi_Cat1_SubOnline" in line:
        in_func = True
        brace_count = 0
        continue
    if in_func:
        brace_count += line.count("{") - line.count("}")
        if brace_count <= 0 and "}" in line:
            # This is the closing brace line
            indent = line[:len(line) - len(line.lstrip())]
            lines.insert(i, indent + "return ESP_OK;")
            print(f"Added return ESP_OK before line {i+1}")
            break
result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\wifi_cat1.c", "wb") as f:
    f.write(result.encode("utf-8"))
print("Fixed wifi_cat1.c")
print("Done")
