#include "app_config.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "esp_partition.h"
#include "wifi_cat1.h"
#include <esp_log.h>
#include <stdio.h>

// 实例化全局变量 info
Info_CB info;
Sys_CB SysCB;
// 运行期统计信息
OTA_ZC_Stats g_ota_zc_stats = {0};
char DeviceNameBuff[SUN_NUMBER + 1][64] = {"GW001", "D001", "D002", "D003"};
char PorductIdBuff[SUN_NUMBER + 1][64] = {GW_PRODUCTID, SUB_PRODUCTID,
                                          SUB_PRODUCTID, SUB_PRODUCTID};

void app_main(void) {
  // 调用移植过来的硬件初始化代码
  extern void bsp_led_init(void);
  bsp_led_init();
  bsp_key_init();

  // 初始化 NVS
  EEprom_Init();

  // =====================================
  // 初始化串口外设 (引脚请根据实际接线修改)
  // =====================================
  // 假设 LoRa 模块接在 TX:17, RX:16，波特率 9600
  bsp_uart_lora_init(17, 16, 9600);

  // 假设 4G Cat1 模块通过 PPPoS 方式初始化 (引脚在 bsp_uart.h 中定义)
  Cat1_PPPoS_Init();

  // 串口发送测试
  bsp_uart_lora_send("Hello LoRa\r\n", 12);
  bsp_uart_cat1_send("AT\r\n", 4);

  // NVS 测试
  ESP_LOGI("MAIN", "Testing NVS...");
  int32_t write_val = 12345;
  int32_t read_val = 0;

  // 测试写入键值对
  ESP_LOGI("MAIN", "Writing %ld to NVS...", write_val);
  EEprom_WriteData("threshold", &write_val, sizeof(write_val));

  // 测试读取键值对
  EEprom_ReadData("threshold", &read_val, sizeof(read_val));
  ESP_LOGI("MAIN", "Read from NVS: %ld", read_val);

  if (write_val == read_val) {
    ESP_LOGI("MAIN", "NVS Test Passed!");
  } else {
    ESP_LOGE("MAIN", "NVS Test Failed!");
  }
}

void start_OTAWriteTask(void *pvParameters) {
  // 如果没有被创建，这里初始化一次（通常可以放在 main 里初始化）
  if (OTA_ZC_Queue == NULL) {
    OTA_ZC_Queue = xQueueCreate(OTA_ZC_QUEUE_LEN, sizeof(OTA_ZC_Chunk *));
  }

  OTA_ZC_Chunk *chunk = NULL;

  ESP_LOGI("OTA", "Zero-Copy OTA Write Task Started");

  for (;;) {
    // 1. 阻塞等待队列中传来指针
    if (xQueueReceive(OTA_ZC_Queue, &chunk, portMAX_DELAY) == pdTRUE) {
      if (chunk == NULL)
        continue;

      // 根据 ota_staflag 决定写入的分区名称
      // 0 写网关自己 -> APP 类型的 "ota_0"
      // 1 写子设备   -> APP 类型的 "ota_1" (或者您定义的数据分区)
      const char *part_label = (chunk->ota_staflag == 1) ? "ota_1" : "ota_0";

      // ESP32 的 ota_0 和 ota_1 默认都是 APP 分区类型
      const esp_partition_t *part = esp_partition_find_first(
          ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, part_label);

      if (part != NULL) {
        esp_err_t err = ESP_FAIL;
        // 注意：page_index 是基于页/分片的偏移，如果每一片大小是 256 字节：
        uint32_t write_offset = chunk->page_index * OTA_RANGE_SIZE;

        // 2. 带重试机制的 Flash 写入
        for (int attempt = 0; attempt < OTA_ZC_WRITE_RETRY_MAX; attempt++) {
          err =
              esp_partition_write(part, write_offset, chunk->data, chunk->len);
          if (err == ESP_OK) {
            g_ota_zc_stats.processed++;
            break;
          }
          g_ota_zc_stats.write_retry++;
          vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (err != ESP_OK) {
          g_ota_zc_stats.write_fail++;
          ESP_LOGE("OTA", "写入分区 %s 失败! offset=0x%08X", part_label,
                   (unsigned int)write_offset);
        } else {
          ESP_LOGD("OTA", "写入成功: offset=0x%08X, len=%d",
                   (unsigned int)write_offset, chunk->len);
        }
      } else {
        ESP_LOGE("OTA", "未找到目标分区 %s", part_label);
      }

      // 3. 【核心】释放这块动态内存（包含了结构体本身和后面的 payload）
      free(chunk);
    }
  }
}
