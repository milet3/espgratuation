with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

brace = 0
for i, line in enumerate(lines):
    brace += line.count("{") - line.count("}")
    if brace < 0:
        print(f"WARNING: Negative brace balance at line {i+1}: {brace}")

print(f"Final brace balance: {brace} (should be 0)")
print(f"Total lines: {len(lines)}")
