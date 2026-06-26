with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Check brace balance line by line, starting from Studio_OTA_CheckTask
brace = 0
started = False
for i, line in enumerate(lines):
    if "void Studio_OTA_CheckTask" in line:
        started = True
        brace = 1  # Opened function
        print(f"Studio_OTA_CheckTask starts at line {i+1}")
        continue
    if started:
        brace += line.count("{") - line.count("}")
        if i > 1399 and i < 1600 and i % 20 == 0:
            print(f"  Line {i+1}: brace={brace}")
        if brace == 0 and "}" in line and i > 1400:
            print(f"Studio_OTA_CheckTask likely ends at line {i+1}, brace={brace}")
            started = False
        if brace < 0:
            print(f"WARNING: Negative brace at line {i+1}: {brace}")
