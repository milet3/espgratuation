with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Check if any #if blocks are open
depth = 0
for i, line in enumerate(lines):
    if line.strip().startswith("#if"):
        depth += 1
    elif line.strip().startswith("#endif"):
        depth -= 1
    if depth < 0:
        print(f"WARNING: Extra #endif at line {i+1}")

print(f"Final #if depth: {depth} (should be 0)")

# Also check brace balance
brace = 0
for i, line in enumerate(lines):
    brace += line.count("{") - line.count("}")
    
print(f"Final brace balance: {brace} (should be 0)")
