with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Fix 1: Look for duplicate TAG definition and remove it
tag_count = 0
new_lines = []
for i, line in enumerate(lines):
    if "static const char *TAG" in line and "WIFI_CAT1" not in line:
        tag_count += 1
        if tag_count > 1:
            # Skip duplicate TAG definitions
            # But also skip the following COMMENT line if present
            continue
    new_lines.append(line)

lines = new_lines
print(f"Removed {tag_count - 1} duplicate TAG definitions")

# Fix 2: Find and fix the enum definition around OTA_CHECK_PARSE
for i, line in enumerate(lines):
    if "OTA_CHECK_PARSE_NO_TASK" in line and "//" not in line:
        # Check surrounding context
        context_before = lines[i-3:i] if i >= 3 else lines[:i]
        context_after = lines[i+1:i+4] if i+4 < len(lines) else lines[i+1:]
        print(f"OTA_CHECK_PARSE at line {i+1}")
        print(f"  Before: {' | '.join(context_before)}")
        print(f"  After: {' | '.join(context_after)}")
        
        # Fix: if the closing } of the enum is missing or if INCONCLUSIVE is missing
        found_close = False
        for j in range(i, min(i+10, len(lines))):
            if "}" in lines[j] and "ota_check_parse_result_t" in lines[j]:
                found_close = True
                print(f"  Enum closing at line {j+1}")
                break
        
        if not found_close and i+3 < len(lines):
            # Add closing brace
            for j in range(i, min(i+10, len(lines))):
                if lines[j].strip() == "" or lines[j].strip().startswith("//"):
                    lines.insert(j, "  OTA_CHECK_PARSE_INCONCLUSIVE")
                    lines.insert(j+1, "} ota_check_parse_result_t;")
                    print(f"  Fixed enum: added INCONCLUSIVE + closing brace")
                    break
        break

result = "\n".join(lines)
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(result.encode("utf-8"))

print("Fixes applied")
