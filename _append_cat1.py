# Read original file lines 2144 to end (0-indexed: 2143 to end)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()

orig_lines = orig_data.decode("utf-8", errors="replace").split("\n")

# Lines to append: 0-indexed 2143 to end (1-indexed 2144-2300)
missing_lines = orig_lines[2143:]  # line 2144 (1-indexed) onwards

# Read current wifi_cat1.c
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\wifi_cat1.c", "rb") as f:
    current_data = f.read()

current_text = current_data.decode("utf-8", errors="replace")

# Remove __attribute__((unused)) from Cat1_Send_AT_Command since it will be defined
current_text = current_text.replace(
    "__attribute__((unused)) static esp_err_t Cat1_Send_AT_Command",
    "static esp_err_t Cat1_Send_AT_Command"
)

# Append missing functions
current_lines = current_text.split("\n")
combined = current_lines + missing_lines
result = "\n".join(combined)

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\wifi_cat1.c", "wb") as f:
    f.write(result.encode("utf-8"))

print(f"Appended {len(missing_lines)} lines from original file")
