with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\_original_wifi_cat1.c", "rb") as f:
    orig_data = f.read()
orig = orig_data.decode("utf-8", errors="replace")
orig_lines = orig.split("\n")

# Extract OTA sections (0-indexed)
ota_sections = [
    orig_lines[46:76],
    orig_lines[76:191],
    orig_lines[192:1303],
    orig_lines[1707:2143],
]

ota_lines = []
for section in ota_sections:
    ota_lines.extend(section)

# Fix garbled Chinese strings in the extracted code
garbled_map = {
    "褰撳墠鍥轰欢鐗堟湰娌℃湁鍙墽琛岀殑 OTA 浠诲姟": "No executable OTA task for this firmware version",
    "OTA 涓嬭浇鍒嗗潡 %d: 鏈 %d 瀛楄妭锛岀疮璁?%u/%d 瀛楄妭": "OTA chunk %d: %d bytes, total %u/%d",
    "姝ｅ湪璇锋眰 Studio OTA 鎺ュ彛: %s": "Requesting Studio OTA endpoint: %s",
    "Studio OTA 杩斿洖鏁版嵁: %s": "Studio OTA response: %s",
    ">>> 鎴愬姛鑾峰彇鍥轰欢涓嬭浇閾炬帴: %s": "Got firmware download URL: %s",
    "寮€濮嬫鏌?OTA 浠诲姟锛屽綋鍓?firmware_version=%s": "Checking OTA tasks, firmware_version=%s",
    "褰撳墠娌℃湁鍖归厤鐨?fuse-ota 浠诲姟: product=%s, device=%s, type=%d, ": "No matching fuse-ota task: product=%s, device=%s, type=%d, ",
    "version=%s銆傞€氬父琛ㄧず杩欏彴璁惧/杩欎釜鐗堟湰褰撳墠娌℃湁寰呮墽琛岀殑鍗囩骇浠诲姟锛?": "version=%s (no pending upgrade, ",
    "鎴栬€呬笂涓€鏉′换鍔″凡缁忚鍏抽棴銆?": "or task was closed)",
    "fuse-ota 鏌ヨ type=%d 鏈尮閰嶅钩鍙颁换鍔★紝缁х画灏濊瘯 type=%d": "fuse-ota query type=%d no match, trying type=%d",
    "鎵€鏈?fuse-ota 妫€鏌ユ帴鍙ｉ兘娌℃湁杩斿洖鍙敤缁撴灉": "All fuse-ota checks returned no result",
    "寮€濮嬬綉鍏?OTA 涓嬭浇: %s": "Starting gateway OTA download: %s",
    "缃戝叧 OTA 澶辫触: %s": "Gateway OTA failed: %s",
    "缃戝叧 OTA 宸插畬鎴愶紝鍑嗗閲嶅惎杩涘叆鏂板浐浠?..": "Gateway OTA complete, rebooting...",
    "閲嶅惎鎴愬姛锛屽凡灏?OTA 闀滃儚鏍囪涓烘湁鏁?": "Reboot success, marked OTA image valid",
    "鏍囪 OTA 闀滃儚涓烘湁鏁堝け璐? %s": "Failed to mark OTA image valid: %s",
    "寰呯‘璁?OTA 鐩爣鐗堟湰=%s锛屼絾褰撳墠 firmware_version=%s锛涙殏涓嶄笂鎶?201": "Pending OTA target=%s current=%s, defer 201",
    "宸蹭笂鎶?OTA 鍗囩骇鎴愬姛锛宖irmware_version=%s": "Reported OTA success, fw=%s",
    "涓婃姤 OTA 鍗囩骇鎴愬姛澶辫触锛屼笅娆″惎鍔ㄦ椂缁х画閲嶈瘯": "OTA report failed, will retry",
    "宸蹭笂鎶?firmware_version=%s": "Reported firmware_version=%s",
    "涓婃姤 firmware_version 澶辫触": "Failed to report firmware_version",
    "MQTT 鏈繛鎺ワ紝璺宠繃 firmware_version 涓婃姤": "MQTT not connected, skip fw report",
    "MQTT 閴存潈 token 涓虹┖锛岃烦杩?OTA 鎴愬姛涓婃姤": "MQTT token empty, skip OTA report",
}

for i, line in enumerate(ota_lines):
    for old, new in garbled_map.items():
        if old in line:
            ota_lines[i] = line.replace(old, new)

# Memory pool code
pool_code = '''
/* =================================================================
 * OTA ZC pre-allocated chunk pool --- zero heap fragmentation
 * ================================================================= */
#define OTA_ZC_CHUNK_SIZE (sizeof(OTA_ZC_Chunk) + OTA_ZC_CHUNK_DATA_MAX)

typedef struct {
  OTA_ZC_Chunk *chunks[OTA_ZC_POOL_SIZE];
  int           free_stack[OTA_ZC_POOL_SIZE];
  int           free_count;
  SemaphoreHandle_t mutex;
  bool initialized;
} OTA_ZC_Pool;

static OTA_ZC_Pool g_ota_zc_pool = { .initialized = false };

void ota_zc_pool_init(void)
{
  if (g_ota_zc_pool.initialized) return;
  g_ota_zc_pool.mutex = xSemaphoreCreateMutex();
  if (g_ota_zc_pool.mutex == NULL) {
    ESP_LOGE(TAG, "ota_zc_pool: mutex fail");
    return;
  }
  int allocated = 0;
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++) {
    g_ota_zc_pool.chunks[i] = (OTA_ZC_Chunk *)heap_caps_malloc(
        OTA_ZC_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
    if (g_ota_zc_pool.chunks[i] == NULL)
      g_ota_zc_pool.chunks[i] = (OTA_ZC_Chunk *)heap_caps_malloc(
          OTA_ZC_CHUNK_SIZE, MALLOC_CAP_INTERNAL);
    if (g_ota_zc_pool.chunks[i] == NULL) break;
    allocated++;
  }
  if (allocated == 0) {
    ESP_LOGE(TAG, "ota_zc_pool: no mem");
    vSemaphoreDelete(g_ota_zc_pool.mutex);
    g_ota_zc_pool.mutex = NULL;
    return;
  }
  for (int i = 0; i < allocated; i++)
    g_ota_zc_pool.free_stack[i] = i;
  g_ota_zc_pool.free_count = allocated;
  g_ota_zc_pool.initialized = true;
  ESP_LOGI(TAG, "ota_zc_pool init: %d chunks x %u bytes",
           allocated, (unsigned int)OTA_ZC_CHUNK_SIZE);
}

void ota_zc_pool_deinit(void)
{
  if (!g_ota_zc_pool.initialized) return;
  if (g_ota_zc_pool.mutex) xSemaphoreTake(g_ota_zc_pool.mutex, portMAX_DELAY);
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++) {
    if (g_ota_zc_pool.chunks[i]) {
      free(g_ota_zc_pool.chunks[i]);
      g_ota_zc_pool.chunks[i] = NULL;
    }
  }
  g_ota_zc_pool.free_count = 0;
  g_ota_zc_pool.initialized = false;
  if (g_ota_zc_pool.mutex) {
    xSemaphoreGive(g_ota_zc_pool.mutex);
    vSemaphoreDelete(g_ota_zc_pool.mutex);
    g_ota_zc_pool.mutex = NULL;
  }
  ESP_LOGI(TAG, "ota_zc_pool deinit");
}

OTA_ZC_Chunk *ota_zc_pool_acquire(uint16_t datalen)
{
  if (!g_ota_zc_pool.initialized || datalen > OTA_ZC_CHUNK_DATA_MAX)
    return NULL;
  OTA_ZC_Chunk *chunk = NULL;
  if (g_ota_zc_pool.mutex)
    xSemaphoreTake(g_ota_zc_pool.mutex, pdMS_TO_TICKS(500));
  if (g_ota_zc_pool.free_count > 0) {
    int idx = g_ota_zc_pool.free_stack[--g_ota_zc_pool.free_count];
    chunk = g_ota_zc_pool.chunks[idx];
    chunk->len = datalen;
  }
  if (g_ota_zc_pool.mutex)
    xSemaphoreGive(g_ota_zc_pool.mutex);
  return chunk;
}

void ota_zc_pool_release(OTA_ZC_Chunk *chunk)
{
  if (!g_ota_zc_pool.initialized || chunk == NULL) return;
  if (g_ota_zc_pool.mutex)
    xSemaphoreTake(g_ota_zc_pool.mutex, pdMS_TO_TICKS(500));
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++) {
    if (g_ota_zc_pool.chunks[i] == chunk) {
      if (g_ota_zc_pool.free_count < OTA_ZC_POOL_SIZE)
        g_ota_zc_pool.free_stack[g_ota_zc_pool.free_count++] = i;
      break;
    }
  }
  if (g_ota_zc_pool.mutex)
    xSemaphoreGive(g_ota_zc_pool.mutex);
}
'''

# OTA notify stubs
notify_stubs = '''
/* OTA notify/reboot coordination stubs */
static bool g_ota_notify_pending = false;

void WiFi_Cat1_RequestOtaNotifyReboot(void) {
    g_ota_notify_pending = true;
    ESP_LOGI(TAG, "OTA notify reboot requested");
}

bool WiFi_Cat1_BeginPendingOtaNotifyBootstrap(void) {
    if (g_ota_notify_pending) {
        g_ota_notify_pending = false;
        ESP_LOGI(TAG, "OTA notify bootstrap started");
        return true;
    }
    return false;
}

void WiFi_Cat1_FinishOtaNotifyBootstrap(void) {
    ESP_LOGI(TAG, "OTA notify bootstrap finished");
}

bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void) {
    return g_ota_notify_pending;
}

void OneNET_FuseOTA_CheckTask(void) {
  Studio_OTA_CheckTask();
}

void WiFi_Cat1_CheckOTATask(uint8_t num) {
  if (num == 0) {
    Studio_OTA_CheckTask();
  }
}
'''

# Now read the base file and append everything
with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "rb") as f:
    base = f.read().decode("utf-8", errors="replace")

full = base + "\n"
full += pool_code + "\n"
full += "\n".join(ota_lines) + "\n"
full += notify_stubs + "\n"

with open(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\components\bsp_uart\ota_manager.c", "wb") as f:
    f.write(full.encode("utf-8"))

print(f"Built ota_manager.c: {len(full.split(chr(10)))} lines")
