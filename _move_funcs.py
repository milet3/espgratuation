# Move the externally-called function definitions to before Studio_OTA_CheckTask
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    data = f.read()
text = data.decode("utf-8", errors="replace")
lines = text.split("\n")

# Find the range of functions to move (lines 1590 to 1773)
move_start = None
move_end = None
for i, line in enumerate(lines):
    if "OTA notify/reboot coordination stubs" in line:
        move_start = i
    if i == len(lines) - 1 and move_start is not None:
        move_end = i
        break

# Find where Studio_OTA_CheckTask starts
insert_at = None
for i, line in enumerate(lines):
    if "void Studio_OTA_CheckTask" in line and "attribute" in line:
        insert_at = i
        break

if move_start and move_end and insert_at:
    moved_lines = lines[move_start:move_end+1]
    before = lines[:insert_at]
    between = lines[insert_at:move_start]
    after = lines[move_end+1:]
    
    new_lines = before + moved_lines + between + after
    result = "\n".join(new_lines)
    
    with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
        f.write(result.encode("utf-8"))
    
    print(f"Moved {len(moved_lines)} lines from after Studio_OTA_CheckTask to before it")
else:
    print(f"Error: move_start={move_start}, move_end={move_end}, insert_at={insert_at}")
