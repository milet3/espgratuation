/*-------------------------------------------------*/
/*                                                 */
/*            操作LoRa模块功能的源文件             */
/*                                                 */
/*-------------------------------------------------*/

#include "lora.h"
#include "app_config.h"
#include "bsp_uart.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"
#include "wifi_cat1.h"

static const char *TAG = "LORA";
LoRaCB lora; // LoRa模块控制结构体
#define HAL_GetTick() ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS))

// FreeRTOS Software Timer Handle for LoRa OTA Timeout
static TimerHandle_t lora_ota_timer = NULL;

// FreeRTOS Software Timer Handle for LoRa OTA Overall 300s Timeout
static TimerHandle_t lora_ota_overall_timer = NULL;

static void lora_ota_overall_timeout_cb(TimerHandle_t xTimer) {
  ESP_LOGW(TAG, "LoRa OTA 全局升级超时 (300s)，结束 OTA 流程");
  if (lora.Ota != 0) {
    lora.Ota = 0;             // 结束OTA传输
    lora.OtaCounter = 0;      // lora.OtaCounter恢复初值
    lora.OtaNum = 0xFFFFFFFF; // lora.OtaNum恢复初值
    if (lora_ota_timer != NULL) {
      xTimerStop(lora_ota_timer, 0); // 同时停止单次重传定时器
    }
  }
}

static void lora_ota_timeout_cb(TimerHandle_t xTimer) {
  ESP_LOGW(TAG, "LoRa OTA 等待超时 (3s)，触发重传机制");
  // 定时器触发，说明超时了没有收到 ACK
  if (lora.Ota != 0) {
    // 执行超时重发逻辑
    // 因为数据包有问题或丢失，重新传输当前的 counter
    LoRa_OTA(lora.OtaCounter);
  }
}

void Partition_Read(uint8_t *pBuffer, uint32_t ReadAddr,
                    uint16_t NumByteToRead) {
  // 根据在 STM32 中的地址 0x50000 偏移量推测，这里原先是读取外部 SPI Flash
  // 的数据 现在切换到 ESP32 的虚拟文件系统或直接读取 OTA 分区。
  // 子设备读取的固件应该存放在 ota_1 分区（网关自己的在 ota_0）
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "ota_1");
  if (partition == NULL) {
    ESP_LOGE(TAG, "未找到名为 'ota_1' 的APP分区，无法读取子设备 OTA 固件数据");
    memset(pBuffer, 0, NumByteToRead);
    return;
  }

  // 从 ota_1 分区的起始位置加上传入的相对偏移地址读取数据
  // 注意：如果是存储在文件系统(SPIFFS/LittleFS)里的文件，建议改成 fopen/fread
  // 操作。 这里采用直接读分区裸数据的方式兼容原先裸写 Flash 的逻辑。
  esp_err_t err =
      esp_partition_read(partition, ReadAddr, pBuffer, NumByteToRead);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "读取 OTA 固件数据失败: %s", esp_err_to_name(err));
    memset(pBuffer, 0, NumByteToRead);
  }
}
/**
 * @brief 擦除子设备 OTA 固件所在的分区
 * @param offset 要擦除的起始偏移地址（如果从头开始擦除就是 0）
 * @param size 要擦除的大小（必须是 4KB 即 0x1000 的整数倍）
 * @return esp_err_t 成功返回 ESP_OK
 */
esp_err_t Partition_Erase(uint32_t offset, size_t size) {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "ota_1");
  if (partition == NULL) {
    ESP_LOGE(TAG, "未找到名为 'ota_1' 的APP分区，无法擦除子设备 OTA 固件数据");
    return ESP_ERR_INVALID_STATE;
  }
  if (offset + size > partition->size) {
    ESP_LOGE(TAG, "擦除范围超出分区大小");
    return ESP_ERR_INVALID_STATE;
  }
  // 执行擦除
  // 注意：擦除 Flash 比较耗时，这期间系统会阻塞，可能会触发看门狗，
  // 建议把比较大的擦除操作放在专门的 Task 里或者喂狗。
  esp_err_t err = esp_partition_erase_range(partition, offset, size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "擦除 OTA 固件失败: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t Partition_write(uint32_t offset, size_t size, uint8_t *data) {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "ota_1");
  if (partition == NULL) {
    ESP_LOGI(TAG, "未找到 'ota_1' 分区，无法写入子设备OTA固件数据");
    return ESP_ERR_INVALID_STATE;
  }
  if (offset + size > partition->size) {
    ESP_LOGE(TAG, "写入范围超出分区大小");
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t err = esp_partition_write(partition, offset, data, size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "写入 OTA 固件失败: %s", esp_err_to_name(err));
  }
  return err;
}

uint16_t CRC16_Modbus(uint8_t *pdata, uint16_t len) {
  uint16_t crc = 0xFFFF;
  int i, j;
  for (i = 0; i < len; i++) {
    crc ^= pdata[i];
    for (j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// u1_printf(...) 被映射为了 ESP_LOGI
// #define u1_printf(...) ESP_LOGI(TAG, __VA_ARGS__)

// u2_printf, u3_printf, u4_printf 以及对应的 TxDataBuf
// 在这里可以直接被替换为对应的串口发送函数

LoRaParameter LoRaSetData = {
    // 模块工作参数
    0x00,             // 模块地址高字节
    0x00,             // 模块地址低字节
    0x00,             // 模块网络地址ID
    LoRa_9600,        // 模块串口 波特率 9600
    LoRa_8N1,         // 串口工作模式 8数据位 无校验 1停止位
    LoRa_19_2,        // 模块空中速率 19.2K
    LoRa_Data240,     // 数据分包大小 240字节
    LoRa_RssiDIS,     // 关闭RSSI功能
    LoRa_SoftDIS,     // 关闭软件切换模块工作模式功能
    LoRa_FEC_22DBM,   // 发射功率22dbm
    LoRa_CH23,        // 模块信道（十进制）
    LoRa_RssiByteDIS, // 禁用RSSI字节功能
    LoRa_ModeTRANS,   // 透传模式
    LoRa_RelayDIS,    // 禁用中继模式
    LoRa_LBTDIS,      // 禁用LBT
    LoRa_WorTX,       // Wor模式发送        只在模式1才有效
    LoRa_Wor2000ms,   // Wor周期2000毫秒    只在模式1才有效
    0x22,             // 模块加密秘钥高字节
    0x33,             // 模块加密秘钥低字节
};

/*-------------------------------------------------*/
/*函数名：初始化模块                               */
/*参  数：无                                       */
/*返回值：0：正确   其他：错误                     */
/*-------------------------------------------------*/
void LoRa_Init(void) {
  uint8_t cmd[12];

  // 初始化GPIO
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << LORA_M0_PIN) | (1ULL << LORA_M1_PIN) |
                      (1ULL << LORA_PWR_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&io_conf);

  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << LORA_AUX_PIN);
  io_conf.pull_up_en = 1;
  gpio_config(&io_conf);

  // 1. 硬件复位序列
  LoRa_PowerOFF;
  vTaskDelay(pdMS_TO_TICKS(200));
  LoRa_PowerON;

  // 2. 等待模块启动完毕（检测 AUX 高电平）
  while (LoRa_AUX != 1) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 3. 进入配置模式 (M0=1, M1=1)
  LoRa_MODE2;
  lora.sta = 0;
  vTaskDelay(pdMS_TO_TICKS(200)); // 模式切换后必须延时，等待模块稳定

  // 5. 组装指令
  cmd[0] = 0xC0;
  cmd[1] = 0x00;
  cmd[2] = 0x09;
  cmd[3] = LoRaSetData.LoRa_AddrH;
  cmd[4] = LoRaSetData.LoRa_AddrL;
  cmd[5] = LoRaSetData.LoRa_NetID;
  cmd[6] = LoRaSetData.LoRa_Baudrate | LoRaSetData.LoRa_UartMode |
           LoRaSetData.LoRa_airvelocity;
  cmd[7] = LoRaSetData.LoRa_DataLen | LoRaSetData.LoRa_Rssi | LoRa_SoftDIS |
           LoRaSetData.LoRa_TxPower;
  cmd[8] = LoRaSetData.LoRa_CH;
  cmd[9] = LoRaSetData.LoRa_RssiByte | LoRaSetData.LoRa_DateMode |
           LoRaSetData.LoRa_Relay | LoRaSetData.LoRa_LBT |
           LoRaSetData.LoRa_WORmode | LoRaSetData.LoRa_WORcycle;
  cmd[10] = LoRaSetData.LoRa_KeyH;
  cmd[11] = LoRaSetData.LoRa_KeyL;

  // 6. 发送配置指令 (使用bsp_uart发送)
  bsp_uart_lora_send((const char *)cmd, 12);

  // 初始化 LoRa OTA 超时定时器 (3000ms, 单次触发不循环)
  if (lora_ota_timer == NULL) {
    lora_ota_timer = xTimerCreate("LoRa_Timeout", pdMS_TO_TICKS(3000), pdFALSE,
                                  (void *)0, lora_ota_timeout_cb);
  }

  // 初始化 LoRa OTA 300秒全局超时定时器 (300,000ms, 单次触发)
  if (lora_ota_overall_timer == NULL) {
    lora_ota_overall_timer =
        xTimerCreate("LoRa_OTA_300s", pdMS_TO_TICKS(300000), pdFALSE, (void *)0,
                     lora_ota_overall_timeout_cb);
  }
}
/*-------------------------------------------------*/
/*函数名：处理LoRa配置状态的数据                   */
/*参  数：data：数据                               */
/*参  数：datalen：数据长度                        */
/*返回值：无                                       */
/*-------------------------------------------------*/
void LoRa_ConfigData(uint8_t *data, uint16_t data_len) {
  if ((data_len == 12) && (data[0] == 0xC1) && (data[1] == 0x00) &&
      (data[2] == 0x09)) { // 接收的数据长度等于12 且 第前三个字节是0xC1 0x00
                           // 0x09 时进入该分支
    while (LoRa_AUX != 1) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }                               // 等到高电平
    LoRa_MODE0;                     // 模式0 正常模式
    vTaskDelay(pdMS_TO_TICKS(200)); // 适当延时
    lora.sta = 1;                   // 进入数据传输状态
    lora.OtaNum = 0xFFFFFFFF; // 子设备OTA升级总共次数初始为0xFFFFFFFF
    ESP_LOGI(TAG, "Lora模块设置指令成功\r\n"); // 串口输出信息
    ESP_LOGI(TAG, "模块地址:0x%02X%02X\r\n", data[3], data[4]); // 串口输出信息
    ESP_LOGI(TAG, "模块网络ID:0x%02X\r\n", data[5]); // 串口输出信息

    switch (data[6] & 0xE0) { // 判断串口波特率
    case LoRa_1200:
      ESP_LOGI(TAG, "波特率 1200\r\n"); // 串口输出信息
      break;                            // 跳出
    case LoRa_2400:
      ESP_LOGI(TAG, "波特率 2400\r\n"); // 串口输出信息
      break;                            // 跳出
    case LoRa_4800:
      ESP_LOGI(TAG, "波特率 4800\r\n"); // 串口输出信息
      break;                            // 跳出
    case LoRa_9600:
      ESP_LOGI(TAG, "波特率 9600\r\n"); // 串口输出信息
      break;                            // 跳出
    case LoRa_19200:
      ESP_LOGI(TAG, "波特率 19200\r\n"); // 串口输出信息
      break;                             // 跳出
    case LoRa_38400:
      ESP_LOGI(TAG, "波特率 38400\r\n"); // 串口输出信息
      break;                             // 跳出
    case LoRa_57600:
      ESP_LOGI(TAG, "波特率 57600\r\n"); // 串口输出信息
      break;                             // 跳出
    case LoRa_115200:
      ESP_LOGI(TAG, "波特率 115200\r\n"); // 串口输出信息
      break;                              // 跳出
    }
    switch (data[6] & 0x18) { // 判断串口工作方式
    case LoRa_8N1:
      ESP_LOGI(TAG, "8数据位 无校验 1停止位\r\n"); // 串口输出信息
      break;                                       // 跳出
    case LoRa_8O1:
      ESP_LOGI(TAG, "8数据位 奇校验 1停止位\r\n"); // 串口输出信息
      break;                                       // 跳出
    case LoRa_8E1:
      ESP_LOGI(TAG, "8数据位 偶校验 1停止位\r\n"); // 串口输出信息
      break;                                       // 跳出
    }
    switch (data[6] & 0x07) { // 判断空中速率
    case LoRa_2_4:
      ESP_LOGI(TAG, "空中速率 2.4K\r\n"); // 串口输出信息
      break;                              // 跳出
    case LoRa_4_8:
      ESP_LOGI(TAG, "空中速率 4.8K\r\n"); // 串口输出信息
      break;                              // 跳出
    case LoRa_9_6:
      ESP_LOGI(TAG, "空中速率 9.6K\r\n"); // 串口输出信息
      break;                              // 跳出
    case LoRa_19_2:
      ESP_LOGI(TAG, "空中速率 19.2K\r\n"); // 串口输出信息
      break;                               // 跳出
    case LoRa_38_4:
      ESP_LOGI(TAG, "空中速率 38.4K\r\n"); // 串口输出信息
      break;                               // 跳出
    case LoRa_62_5:
      ESP_LOGI(TAG, "空中速率 62.5K\r\n"); // 串口输出信息
      break;
    }

    switch (data[7] & 0xC0) { // 判断数据分包大小
    case LoRa_Data240:
      ESP_LOGI(TAG, "数据分包大小：240字节\r\n"); // 串口输出信息
      break;                                      // 跳出
    case LoRa_Data128:
      ESP_LOGI(TAG, "数据分包大小：128字节\r\n"); // 串口输出信息
      break;                                      // 跳出
    case LoRa_Data64:
      ESP_LOGI(TAG, "数据分包大小：64字节\r\n"); // 串口输出信息
      break;                                     // 跳出
    case LoRa_Data32:
      ESP_LOGI(TAG, "数据分包大小：32字节\r\n"); // 串口输出信息
      break;                                     // 跳出
    }

    switch (data[7] & 0x20) { // 判断是否启用RSSI功能
    case LoRa_RssiEN:
      ESP_LOGI(TAG, "启用RSSI功能\r\n"); // 串口输出信息
      break;                             // 跳出
    case LoRa_RssiDIS:
      ESP_LOGI(TAG, "禁用RSSI功能\r\n"); // 串口输出信息
      break;                             // 跳出
    }

    switch (data[7] & 0x04) { // 判断是否启用RSSI功能
    case LoRa_SoftEN:
      ESP_LOGI(TAG, "启用软件切换模块工作模式功能\r\n"); // 串口输出信息
      break;                                             // 跳出
    case LoRa_SoftDIS:
      ESP_LOGI(TAG, "禁用软件切换模块工作模式功能\r\n"); // 串口输出信息
      break;                                             // 跳出
    }

    switch (data[7] & 0x03) { // 判断发射功率
    case LoRa_FEC_22DBM:
      ESP_LOGI(TAG, "发射功率 22dbm\r\n"); // 串口输出信息
      break;                               // 跳出
    case LoRa_FEC_17DBM:
      ESP_LOGI(TAG, "发射功率 17dbm\r\n"); // 串口输出信息
      break;                               // 跳出
    case LoRa_FEC_13DBM:
      ESP_LOGI(TAG, "发射功率 13dbm\r\n"); // 串口输出信息
      break;                               // 跳出
    case LoRa_FEC_10DBM:
      ESP_LOGI(TAG, "发射功率 10dbm\r\n"); // 串口输出信息
      break;                               // 跳出
    }

    ESP_LOGI(TAG, "信道:0x%02X  对应频率%dMHz\r\n", data[8] & 0x7F,
             410 + (data[8] & 0x7F)); // 串口输出信息

    switch (data[9] & 0x80) { // 判断RSSI字节功能
    case LoRa_RssiByteEN:
      ESP_LOGI(TAG, "启用RSSI字节功能\r\n"); // 串口输出信息
      break;                                 // 跳出
    case LoRa_RssiByteDIS:
      ESP_LOGI(TAG, "禁用RSSI字节功能\r\n"); // 串口输出信息
      break;                                 // 跳出
    }
    switch (data[9] & 0x40) { // 判断传输模式
    case LoRa_ModeTRANS:
      ESP_LOGI(TAG, "透明传输\r\n"); // 串口输出信息
      break;                         // 跳出
    case LoRa_ModePOINT:
      ESP_LOGI(TAG, "定点传输\r\n"); // 串口输出信息
      break;                         // 跳出
    }
    switch (data[9] & 0x20) { // 判断中继功能
    case LoRa_RelayEN:
      ESP_LOGI(TAG, "启用中继\r\n"); // 串口输出信息
      break;                         // 跳出
    case LoRa_RelayDIS:
      ESP_LOGI(TAG, "禁用中继\r\n"); // 串口输出信息
      break;                         // 跳出
    }
    switch (data[9] & 0x10) { // 判断LBT功能
    case LoRa_LBTEN:
      ESP_LOGI(TAG, "启用LBT\r\n"); // 串口输出信息
      break;                        // 跳出
    case LoRa_LBTDIS:
      ESP_LOGI(TAG, "禁用LBT\r\n"); // 串口输出信息
      break;                        // 跳出
    }
    switch (data[9] & 0x08) { // 判断WOR模式
    case LoRa_WorTX:
      ESP_LOGI(TAG, "Wor模式发送(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                             // 跳出
    case LoRa_WorRX:
      ESP_LOGI(TAG, "Wor模式接收(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                             // 跳出
    }
    switch (data[9] & 0x07) { // 判断WOR周期
    case LoRa_Wor500ms:
      ESP_LOGI(TAG, "WOR周期时间 500毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                     // 跳出
    case LoRa_Wor1000ms:
      ESP_LOGI(TAG,
               "WOR周期时间 1000毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                 // 跳出
    case LoRa_Wor1500ms:
      ESP_LOGI(TAG,
               "WOR周期时间 1500毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                 // 跳出
    case LoRa_Wor2000ms:
      ESP_LOGI(TAG,
               "WOR周期时间 2000毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                 // 跳出
    case LoRa_Wor2500ms:
      ESP_LOGI(TAG,
               "WOR周期时间 2500毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                 // 跳出
    case LoRa_Wor3000ms:
      ESP_LOGI(TAG,
               "WOR周期时间 3000毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                 // 跳出
    case LoRa_Wor3500ms:
      ESP_LOGI(TAG,
               "WOR周期时间 3500毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                 // 跳出
    case LoRa_Wor4000ms:
      ESP_LOGI(TAG,
               "WOR周期时间 4000毫秒(仅模式1下才有效)\r\n"); // 串口输出信息
      break;                                                 // 跳出
    }
  }
}
/*-------------------------------------------------*/
/*函数名：处理LoRa传输状态的数据                   */
/*参  数：data：数据                               */
/*参  数：datalen：数据长度                        */
/*返回值：无                                       */
/*-------------------------------------------------*/
void LoRa_TransData(uint8_t *data, uint16_t data_len) {
  uint32_t temp_crc;
  uint8_t tempbuff[512];
  char temp[4][10];

  temp_crc = CRC16_Modbus(data, data_len - 2); // 计算CRC
  if (temp_crc ==
      data[data_len - 2] * 256 + data[data_len - 1]) { // CRC正确进入该分支
    lora.timeout[data[0]] = 0; // 子设备回复数据了，清除超时次数变量
    if (lora.online[data[0]] == 0) { // 如果子设备当前下线状态，进入该分支
      if ((data[1] == 0x03) &&
          (data[2] ==
           0x06)) { // 如果是03功能码 且 共计6字节数据，是子设备上报的版本号
        memset(lora.SubVer[data[0]], 0, VERSION_LEN + 1); // 清空缓冲区
        memcpy(lora.SubVer[data[0]], &data[3],
               VERSION_LEN); // 拷贝子设备上报的版本号数据
        ESP_LOGI(TAG, "获取子设备%d版本号：%s\r\n", data[0],
                 lora.SubVer[data[0]]); // 串口输出信息
        if (memcmp(info.Version[1], lora.SubVer[data[0]], VERSION_LEN) ==
            0) { // 比较子设备上报的版本号,一样的话进入if
          ESP_LOGI(TAG, "子设备%d无需OTA升级\r\n", data[0]); // 串口输出信息
          ESP_LOGI(TAG, "发送子设备%d上线数据\r\n", data[0]); // 串口输出信息
          WiFi_Cat1_SubOnline(data[0], 1); // 发送子设备上线数据
        } else { // 比较子设备上报的版本号,不一样的话进入else
          ESP_LOGI(TAG, "子设备%d需OTA升级\r\n", data[0]); // 串口输出信息
          tempbuff[0] = data[0];                           // 从机地址
          tempbuff[1] = 0x10;                              // 10功能码
          tempbuff[2] = VERSION1_REGISTER / 256; // 寄存器起始地址高字节
          tempbuff[3] = VERSION1_REGISTER % 256; // 寄存器起始地址低字节
          tempbuff[4] = 0x00;                    // 读寄存器个数高字节
          tempbuff[5] = 0x03;                    // 读寄存器个数低字节
          tempbuff[6] = 0x06;                    // 共计6字节数据
          memcpy(&tempbuff[7], info.Version[1], 6);         // 拷贝数据
          temp_crc = CRC16_Modbus((uint8_t *)tempbuff, 13); // 计算CRC
          tempbuff[13] = temp_crc / 256;                    // CRC高字节
          tempbuff[14] = temp_crc % 256;                    // CRC低字节
          bsp_uart_lora_send((const char *)tempbuff, 15); // 加入LoRa缓冲区
        }
      } else if (data[1] == 0x10) { // 如果是10功能码
        lora.Ota = data[0];         // 记录那个子设备需要OTA
        ESP_LOGI(TAG, "子设备%d开始OTA升级\r\n", lora.Ota); // 串口输出信息

        // 启动或重置全局300秒超时定时器
        if (lora_ota_overall_timer != NULL) {
          xTimerReset(lora_ota_overall_timer, 0);
        }

        if ((info.OTA_firelen[1] % 256) == 0) {        // 计算传输次数
          lora.OtaNum = info.OTA_firelen[1] / 256;     // 计算传输次数
        } else {                                       // 计算传输次数
          lora.OtaNum = info.OTA_firelen[1] / 256 + 1; // 计算传输次数
        }
        lora.OtaCounter = 1;       // 准备传输次数等于1
        LoRa_OTA(lora.OtaCounter); // 开始传输
      }
    } else { // 如果子设备当前上线状态，进入该分支
      if (data[1] == 0x04) {             // 如果是04功能码
        memset(temp, 0, 40);             // 清空缓冲区
        lora.SW_Sta[data[0]] = data[10]; // 记录开关状态
        if ((data[10] & 0x01) ==
            0) // 判断子设备回复的开关状态数据中，开关1的状态，if成立表示关闭状态
          sprintf(&temp[0][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关1的状态，else成立表示打开状态
          sprintf(&temp[0][0], "true"); // 缓冲区写入true，表示打开状态

        if ((data[10] & 0x02) ==
            0) // 判断子设备回复的开关状态数据中，开关2的状态，if成立表示关闭状态
          sprintf(&temp[1][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关2的状态，else成立表示打开状态
          sprintf(&temp[1][0], "true"); // 缓冲区写入true，表示打开状态

        if ((data[10] & 0x04) ==
            0) // 判断子设备回复的开关状态数据中，开关3的状态，if成立表示关闭状态
          sprintf(&temp[2][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关3的状态，else成立表示打开状态
          sprintf(&temp[2][0], "true"); // 缓冲区写入true，表示打开状态

        if ((data[10] & 0x08) ==
            0) // 判断子设备回复的开关状态数据中，开关4的状态，if成立表示关闭状态
          sprintf(&temp[3][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关4的状态，else成立表示打开状态
          sprintf(&temp[3][0], "true"); // 缓冲区写入true，表示打开状态

        memset(tempbuff, 0, 512); // 临时缓冲区全部清零
        sprintf((char *)tempbuff,
                "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":[{\"identity\":{"
                "\"productID\":\"%s\",\"deviceName\":\"%s\"},\"properties\":{"
                "\"%s\":{\"value\":%s},\"%s\":{\"value\":%s},\"%s\":{\"value\":"
                "%s},\"%s\":{\"value\":%s},\"%s\":{\"value\":%.2f},\"%s\":{"
                "\"value\":%.2f},\"%s\":{\"value\":%.2f},\"%s\":{\"value\":%."
                "2f},\"%s\":{\"value\":%.2f},\"%s\":{\"value\":%.2f}}}]}",
                data[0], SUB_PRODUCTID, DeviceNameBuff[data[0]], ATTRIBUTE1,
                &temp[0][0], ATTRIBUTE2, &temp[1][0], ATTRIBUTE3, &temp[2][0],
                ATTRIBUTE4, &temp[3][0], ATTRIBUTE5, data[3] + data[4] / 10.0,
                ATTRIBUTE6, data[5] + data[6] / 10.0, ATTRIBUTE7,
                (data[7] * 256 + data[8]) / 1.2, ATTRIBUTE8,
                (data[11] * 256 + data[12]) * (3.3 / 4096.0), ATTRIBUTE9,
                (data[13] * 256 + data[14]) * (3.3 / 4096.0), ATTRIBUTE10,
                (data[15] * 256 + data[16]) * (3.3 / 4096.0)); // 构建数据
        WiFi_Cat1_SubDataPost((unsigned char *)tempbuff); // 子设备发送数据
      } else if (data[1] == 0x06) {     // 如果是06功能码
        memset(temp, 0, 40);            // 清空缓冲区
        lora.SW_Sta[data[0]] = data[5]; // 记录开关状态
        if ((data[5] & 0x01) ==
            0) // 判断子设备回复的开关状态数据中，开关1的状态，if成立表示关闭状态
          sprintf(&temp[0][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关1的状态，else成立表示打开状态
          sprintf(&temp[0][0], "true"); // 缓冲区写入true，表示打开状态

        if ((data[5] & 0x02) ==
            0) // 判断子设备回复的开关状态数据中，开关2的状态，if成立表示关闭状态
          sprintf(&temp[1][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关2的状态，else成立表示打开状态
          sprintf(&temp[1][0], "true"); // 缓冲区写入true，表示打开状态

        if ((data[5] & 0x04) ==
            0) // 判断子设备回复的开关状态数据中，开关3的状态，if成立表示关闭状态
          sprintf(&temp[2][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关3的状态，else成立表示打开状态
          sprintf(&temp[2][0], "true"); // 缓冲区写入true，表示打开状态

        if ((data[5] & 0x08) ==
            0) // 判断子设备回复的开关状态数据中，开关4的状态，if成立表示关闭状态
          sprintf(&temp[3][0], "false"); // 缓冲区写入false，表示关闭状态
        else // 判断子设备回复的开关状态数据中，开关4的状态，else成立表示打开状态
          sprintf(&temp[3][0], "true"); // 缓冲区写入true，表示打开状态

        memset(tempbuff, 0, 512); // 临时缓冲区全部清零
        sprintf((char *)tempbuff,
                "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":[{\"identity\":{"
                "\"productID\":\"%s\",\"deviceName\":\"%s\"},\"properties\":{"
                "\"%s\":{\"value\":%s},\"%s\":{\"value\":%s},\"%s\":{\"value\":"
                "%s},\"%s\":{\"value\":%s}}}]}",
                data[0], SUB_PRODUCTID, DeviceNameBuff[data[0]], ATTRIBUTE1,
                &temp[0][0], ATTRIBUTE2, &temp[1][0], ATTRIBUTE3, &temp[2][0],
                ATTRIBUTE4, &temp[3][0]);                 // 构建数据
        WiFi_Cat1_SubDataPost((unsigned char *)tempbuff); // 子设备发送数据
      }
    }
  } else {                            // CRC错误，进入else分支
    ESP_LOGI(TAG, "CRC校验错误\r\n"); // 串口输出信息
  }
}
/*-------------------------------------------------*/
/*函数名：处理LoRa接收到的OTA数据                  */
/*参  数：data：数据                               */
/*参  数：datalen：数据长度                        */
/*返回值：无                                       */
/*-------------------------------------------------*/
void LoRa_ProcessOTA(uint8_t *data, uint16_t data_len) {
  uint8_t tempbuff[12];

  if ((data[0] == lora.Ota) && (data[1] == 0x06) &&
      (data_len == 2)) {                  // 收到0x06，表示从机应答
    if (lora.OtaCounter == lora.OtaNum) { // if成立，表示所有的数据包都发完了
      tempbuff[0] = lora.Ota;             // 子设备地址
      tempbuff[1] = 0x04;                 // 结束符
      bsp_uart_lora_send((const char *)tempbuff,
                         2); // 加入LoRa缓冲区，通知节点都发送完了
    } else {                 // 如果不是最后一次，进入该分支
      lora.OtaCounter++;     // 传输次数+1
      LoRa_OTA(lora.OtaCounter); // 开始传输
    }
  } else if ((data[0] == lora.Ota) && (data[1] == 0x15) &&
             (data_len ==
              2)) { // 收到0x15，表示从机非应答，数据包有问题，需要重发
    LoRa_OTA(lora.OtaCounter); // 传输次数不变，重新传输
  } else if ((data[0] == lora.Ota) && (data[1] == 0x04) &&
             (data_len == 2)) { // 收到0x04，表示从机收到结束符
    ESP_LOGI(TAG, "子设备%d OTA升级完毕\r\n", lora.Ota); // 串口输出信息
    lora.Ota = 0;                                        // 结束OTA传输
    lora.OtaCounter = 0;      // lora.OtaCounter恢复初值
    lora.OtaNum = 0xFFFFFFFF; // lora.OtaNum恢复初值

    // 接收到节点回复的数据，关闭软件定时器
    if (lora_ota_timer != NULL) {
      xTimerStop(lora_ota_timer, 0);
    }

    // 升级完毕，同时关闭300秒全局超时定时器
    if (lora_ota_overall_timer != NULL) {
      xTimerStop(lora_ota_overall_timer, 0);
    }
  }
}
/*-------------------------------------------------*/
/*函数名：LoRa子设备OTA传输数据                    */
/*参  数：counter:第几次传输                       */
/*返回值：无                                       */
/*-------------------------------------------------*/
void LoRa_OTA(uint16_t counter) {
  uint32_t temp_crc;

  lora.OTA_Buff[0] = lora.Ota;      // 子设备地址
  lora.OTA_Buff[1] = counter / 256; // 第几次传输高字节
  lora.OTA_Buff[2] = counter % 256; // 第几次传输低字节
  Partition_Read(&lora.OTA_Buff[3], 0x50000 + (counter - 1) * 256,
                 256);                                  // 读256字节数据
  temp_crc = CRC16_Modbus(lora.OTA_Buff, 259);          // 计算CRC
  lora.OTA_Buff[259] = temp_crc / 256;                  // CRC高字节
  lora.OTA_Buff[260] = temp_crc % 256;                  // CRC低字节
  bsp_uart_lora_send((const char *)lora.OTA_Buff, 261); // 加入LoRa缓冲区
  ESP_LOGI(TAG, "当前子设备%d第%d/%d次传输\r\n", lora.Ota, counter,
           lora.OtaNum); // 串口输出信息

  // 发送完数据后，启动或重置3秒超时定时器
  if (lora_ota_timer != NULL) {
    xTimerReset(lora_ota_timer, 0);
  }
}
/*-------------------------------------------------*/
/*函数名：主动事件                                 */
/*参  数：无                                       */
/*返回值：无                                       */
/*-------------------------------------------------*/
void LoRa_ActiveEvent(void) {
  uint8_t tempbuff[64], adr;
  uint32_t temp_crc;

  // 连接上服务器且不是OTA服务器 且 间隔够3s 且 LoRa_sta是1(传输状态) 且
  // 无OTA时，进入分支
  if ((SysCB.SysEventFlag & CONNECT_MQTT) &&
      ((HAL_GetTick() - lora.timer) >= 3000) && (lora.sta == 1) &&
      (lora.Ota == 0)) {
    lora.timer = HAL_GetTick(); // 记录当前时间值
    adr = lora.counter % SUN_NUMBER + 1; // 计算本次需要操作的lora节点从机地址
    if (lora.online[adr] == 0) {             // 不在线时，进入该分支
      tempbuff[0] = adr;                     // 从机地址
      tempbuff[1] = 0x03;                    // 03功能码
      tempbuff[2] = VERSION1_REGISTER / 256; // 寄存器起始地址高字节
      tempbuff[3] = VERSION1_REGISTER % 256; // 寄存器起始地址低字节
      tempbuff[4] = 0x00;                    // 读寄存器个数高字节
      tempbuff[5] = 0x03;                    // 读寄存器个数低字节
      temp_crc = CRC16_Modbus((uint8_t *)tempbuff, 6); // 计算CRC
      tempbuff[6] = temp_crc / 256;                    // CRC高字节
      tempbuff[7] = temp_crc % 256;                    // CRC低字节
      bsp_uart_lora_send((const char *)tempbuff, 8);   // 加入LoRa缓冲区
    } else if ((lora.online[adr] == 1) &&
               (lora.timeout[adr] >=
                3)) { // 在线状态是1 且 超时次数大于等于3次，进入else
                      // if，认定子设备不在线了
      int prev_timeout = lora.timeout[adr];
      lora.timeout[adr] = 0; // 清零超时次数变量
      ESP_LOGI(TAG, "发送子设备%d下线数据\r\n", adr); // 串口输出信息
      WiFi_Cat1_SubOnline(adr, 0); // 发送子设备下线数据

      // 通过业务通道上报一条“子设备下线”的事件，供手机 App 订阅/展示
      {
        char postdata[192];
        memset(postdata, 0, sizeof(postdata));
        sprintf(postdata,
                "{\"event\":\"sub_offline\",\"id\":%d,\"name\":\"%s\","
                "\"reason\":\"read_timeout\",\"threshold\":3,\"timeoutCount\":%"
                "d,\"channel\":\"LoRa\",\"ts\":%lu}",
                adr, DeviceNameBuff[adr], prev_timeout,
                (unsigned long)HAL_GetTick());
        WiFi_Cat1_SubDataPost((unsigned char *)postdata);
      }
    } else { // 在线状态且超时次数不超过阈值，进入else
      tempbuff[0] = adr;                 // 从机地址
      tempbuff[1] = 0x04;                // 04功能码
      tempbuff[2] = TEMP_REGISTER / 256; // 寄存器起始地址高字节
      tempbuff[3] = TEMP_REGISTER % 256; // 寄存器起始地址低字节
      tempbuff[4] = 0x00;                // 读寄存器个数高字节
      tempbuff[5] = 0x07;                // 读寄存器个数低字节
      temp_crc = CRC16_Modbus((uint8_t *)tempbuff, 6); // 计算CRC
      tempbuff[6] = temp_crc / 256;                    // CRC高字节
      tempbuff[7] = temp_crc % 256;                    // CRC低字节
      bsp_uart_lora_send((const char *)tempbuff, 8);   // 加入LoRa缓冲区
      if (lora.online[adr] == 1) { // 如果当前设备在线状态，进入该分支
        lora.timeout[adr]++; // LoRa读取子设备数据累计次数变量加1次
      }
    }
    lora.counter++; // 读取节点的计数变量+1
  }
}

void LoRa_SendData(uint8_t *data, uint16_t len) {
  bsp_uart_lora_send((const char *)data, len);
}
