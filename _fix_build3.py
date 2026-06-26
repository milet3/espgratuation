with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()

text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Fix: Add OTA_CHECK_PARSE_INCONCLUSIVE to the enum
for i, line in enumerate(lines):
    if "OTA_CHECK_PARSE_NO_TASK" in line and "enum" not in line and i+1 < len(lines) and "}" in lines[i+1]:
        # This is the last enum value before closing brace
        # Replace the closing line to add INCONCLUSIVE
        indent = line[:len(line) - len(line.lstrip())]
        lines[i] = line.rstrip(",") + ","
        lines.insert(i+1, indent + "OTA_CHECK_PARSE_INCONCLUSIVE")
        print(f"Added OTA_CHECK_PARSE_INCONCLUSIVE after line {i+1}")
        break

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))
print("Done")
