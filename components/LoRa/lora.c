/*-------------------------------------------------*/
/*                                                 */
/*            操作LoRa模块功能的源文件             */
/*                                                 */
/*-------------------------------------------------*/

#include "lora.h"
#include "app_config.h"
#include "bsp_uart.h"
#include "driver_llcc68.h"
#include "driver_llcc68_interface.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"
#include "wifi_cat1.h"
#include <stdint.h>

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
          LoRa_SendData((uint8_t *)tempbuff, 15); // 加入LoRa缓冲区
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
      LoRa_SendData((uint8_t *)tempbuff,
                    2);  // 加入LoRa缓冲区，通知节点都发送完了
    } else {             // 如果不是最后一次，进入该分支
      lora.OtaCounter++; // 传输次数+1
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
                 256);                          // 读256字节数据
  temp_crc = CRC16_Modbus(lora.OTA_Buff, 259);  // 计算CRC
  lora.OTA_Buff[259] = temp_crc / 256;          // CRC高字节
  lora.OTA_Buff[260] = temp_crc % 256;          // CRC低字节
  LoRa_SendData((uint8_t *)lora.OTA_Buff, 261); // 加入LoRa缓冲区
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
      LoRa_SendData((uint8_t *)tempbuff, 8);           // 加入LoRa缓冲区
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
      LoRa_SendData((uint8_t *)tempbuff, 8);           // 加入LoRa缓冲区
      if (lora.online[adr] == 1) { // 如果当前设备在线状态，进入该分支
        lora.timeout[adr]++; // LoRa读取子设备数据累计次数变量加1次
      }
    }
    lora.counter++; // 读取节点的计数变量+1
  }
}

// 定义全局结构体对象
static llcc68_handle_t gs_handle;

/**
 * @brief LoRa GPIO 中断处理函数 (DIO1)
 */
static SemaphoreHandle_t lora_irq_sem = NULL;

// 修正后的 ISR：只负责发信号
static void IRAM_ATTR lora_gpio_isr_handler(void *arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(lora_irq_sem, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

// 新增 IRQ 处理任务：负责跑 SPI 逻辑
void lora_irq_task(void *pvParameters) {
  while (1) {
    // 阻塞等待信号量，收到中断信号后处理
    if (xSemaphoreTake(lora_irq_sem, portMAX_DELAY) == pdTRUE) {
      llcc68_irq_handler(&gs_handle);
    }
  }
}

// 新增 LoRa 业务处理任务
void lora_process_task(void *pvParameters) {
  // 初始进入接收模式
  LoRa_EnterRxMode();

  while (1) {
    LoRa_ActiveEvent();
    vTaskDelay(pdMS_TO_TICKS(100)); // 检查频率
  }
}

uint8_t LoRa_Init(void) {
  ESP_LOGI(TAG, "LoRa_Init\r\n");
  uint8_t res;
  uint32_t reg;
  uint8_t modulation;
  uint8_t config;

  // 初始化信号量
  if (lora_irq_sem == NULL) {
    lora_irq_sem = xSemaphoreCreateBinary();
  }

  /* 配置射频开关控制引脚 (TXEN, RXEN) */
  gpio_config_t rf_sw_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << LORA_TXEN_PIN) | (1ULL << LORA_RXEN_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&rf_sw_conf);

  /* 配置 DIO1 中断引脚 */
  gpio_config_t dio1_conf = {
      .intr_type = GPIO_INTR_POSEDGE, // LLCC68 中断为上升沿触发
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << LORA_DIO1_PIN),
      .pull_down_en = 1,
      .pull_up_en = 0,
  };
  gpio_config(&dio1_conf);

  /* 安装 GPIO 中断服务并添加处理函数 */
  gpio_install_isr_service(0);
  gpio_isr_handler_add(LORA_DIO1_PIN, lora_gpio_isr_handler,
                       (void *)LORA_DIO1_PIN);

  /* link interface function */
  DRIVER_LLCC68_LINK_INIT(&gs_handle, llcc68_handle_t);
  DRIVER_LLCC68_LINK_SPI_INIT(&gs_handle, llcc68_interface_spi_init);
  DRIVER_LLCC68_LINK_SPI_DEINIT(&gs_handle, llcc68_interface_spi_deinit);
  DRIVER_LLCC68_LINK_SPI_WRITE_READ(&gs_handle,
                                    llcc68_interface_spi_write_read);
  DRIVER_LLCC68_LINK_RESET_GPIO_INIT(&gs_handle,
                                     llcc68_interface_reset_gpio_init);
  DRIVER_LLCC68_LINK_RESET_GPIO_DEINIT(&gs_handle,
                                       llcc68_interface_reset_gpio_deinit);
  DRIVER_LLCC68_LINK_RESET_GPIO_WRITE(&gs_handle,
                                      llcc68_interface_reset_gpio_write);
  DRIVER_LLCC68_LINK_BUSY_GPIO_INIT(&gs_handle,
                                    llcc68_interface_busy_gpio_init);
  DRIVER_LLCC68_LINK_BUSY_GPIO_DEINIT(&gs_handle,
                                      llcc68_interface_busy_gpio_deinit);
  DRIVER_LLCC68_LINK_BUSY_GPIO_READ(&gs_handle,
                                    llcc68_interface_busy_gpio_read);
  DRIVER_LLCC68_LINK_DELAY_MS(&gs_handle, llcc68_interface_delay_ms);
  DRIVER_LLCC68_LINK_DEBUG_PRINT(&gs_handle, llcc68_interface_debug_print);
  DRIVER_LLCC68_LINK_RECEIVE_CALLBACK(&gs_handle,
                                      llcc68_interface_receive_callback);

  /* init the llcc68 */
  res = llcc68_init(&gs_handle);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: init failed.\n");

    return 1;
  }

  /* enter standby */
  res = llcc68_set_standby(&gs_handle, LLCC68_CLOCK_SOURCE_XTAL_32MHZ);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set standby failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set stop timer on preamble */
  res = llcc68_set_stop_timer_on_preamble(
      &gs_handle, LLCC68_LORA_DEFAULT_STOP_TIMER_ON_PREAMBLE);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: stop timer on preamble failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set regulator mode */
  res =
      llcc68_set_regulator_mode(&gs_handle, LLCC68_LORA_DEFAULT_REGULATOR_MODE);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set regulator mode failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set pa config */
  res =
      llcc68_set_pa_config(&gs_handle, LLCC68_LORA_DEFAULT_PA_CONFIG_DUTY_CYCLE,
                           LLCC68_LORA_DEFAULT_PA_CONFIG_HP_MAX);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set pa config failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* enter to stdby xosc mode */
  res = llcc68_set_rx_tx_fallback_mode(&gs_handle,
                                       LLCC68_RX_TX_FALLBACK_MODE_STDBY_XOSC);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set rx tx fallback mode failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set dio irq */
  res = llcc68_set_dio_irq_params(&gs_handle, 0x03FF, 0x03FF, 0x0000, 0x0000);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set dio irq params failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* clear irq status */
  res = llcc68_clear_irq_status(&gs_handle, 0x03FF);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: clear irq status failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set lora mode */
  res = llcc68_set_packet_type(&gs_handle, LLCC68_PACKET_TYPE_LORA);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set packet type failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set tx params */
  res = llcc68_set_tx_params(&gs_handle, LLCC68_LORA_DEFAULT_TX_DBM,
                             LLCC68_LORA_DEFAULT_RAMP_TIME);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set tx params failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set lora modulation params */
  res = llcc68_set_lora_modulation_params(
      &gs_handle, LLCC68_LORA_DEFAULT_SF, LLCC68_LORA_DEFAULT_BANDWIDTH,
      LLCC68_LORA_DEFAULT_CR, LLCC68_LORA_DEFAULT_LOW_DATA_RATE_OPTIMIZE);
  if (res != 0) {
    llcc68_interface_debug_print(
        "llcc68: set lora modulation params failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* convert the frequency */
  res = llcc68_frequency_convert_to_register(
      &gs_handle, LLCC68_LORA_DEFAULT_RF_FREQUENCY, (uint32_t *)&reg);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: convert to register failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set the frequency */
  res = llcc68_set_rf_frequency(&gs_handle, reg);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set rf frequency failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set base address */
  res = llcc68_set_buffer_base_address(&gs_handle, 0x00, 0x00);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set buffer base address failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set lora symb num */
  res = llcc68_set_lora_symb_num_timeout(&gs_handle,
                                         LLCC68_LORA_DEFAULT_SYMB_NUM_TIMEOUT);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set lora symb num timeout failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* reset stats */
  res = llcc68_reset_stats(&gs_handle, 0x0000, 0x0000, 0x0000);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: reset stats failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* clear device errors */
  res = llcc68_clear_device_errors(&gs_handle);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: clear device errors failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set the lora sync word */
  res = llcc68_set_lora_sync_word(&gs_handle, LLCC68_LORA_DEFAULT_SYNC_WORD);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set lora sync word failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* get tx modulation */
  res = llcc68_get_tx_modulation(&gs_handle, (uint8_t *)&modulation);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: get tx modulation failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }
  modulation |= 0x04;

  /* set the tx modulation */
  res = llcc68_set_tx_modulation(&gs_handle, modulation);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set tx modulation failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set the rx gain */
  res = llcc68_set_rx_gain(&gs_handle, LLCC68_LORA_DEFAULT_RX_GAIN);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set rx gain failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* set the ocp */
  res = llcc68_set_ocp(&gs_handle, LLCC68_LORA_DEFAULT_OCP);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set ocp failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

  /* get the tx clamp config */
  res = llcc68_get_tx_clamp_config(&gs_handle, (uint8_t *)&config);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: get tx clamp config failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }
  config |= 0x1E;

  /* set the tx clamp config */
  res = llcc68_set_tx_clamp_config(&gs_handle, config);
  if (res != 0) {
    llcc68_interface_debug_print("llcc68: set tx clamp config failed.\n");
    (void)llcc68_deinit(&gs_handle);

    return 1;
  }

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

  // 创建 IRQ 处理任务
  xTaskCreate(lora_irq_task, "lora_irq_task", 4096, NULL, 15, NULL);
  // 创建业务处理任务
  xTaskCreate(lora_process_task, "lora_process_task", 4096, NULL, 12, NULL);

  ESP_LOGI(TAG, "LoRa_Init: success");
  return 0;
}

uint8_t LoRa_SendData(uint8_t *data, uint16_t len) {
  TXEN_HIGH();
  RXEN_LOW();
  // 1、进入发送模式
  if (llcc68_set_dio_irq_params(
          &gs_handle,
          LLCC68_IRQ_TX_DONE | LLCC68_IRQ_TIMEOUT | LLCC68_IRQ_CAD_DONE |
              LLCC68_IRQ_CAD_DETECTED,
          LLCC68_IRQ_TX_DONE | LLCC68_IRQ_TIMEOUT | LLCC68_IRQ_CAD_DONE |
              LLCC68_IRQ_CAD_DETECTED,
          0x0000, 0x0000) != 0) {
    return 1;
  }

  /* clear irq status */
  if (llcc68_clear_irq_status(&gs_handle, 0x03FFU) != 0) {
    return 1;
  }

  // 2、发送数据
  /* sent the data */
  if (llcc68_lora_transmit(
          &gs_handle, LLCC68_CLOCK_SOURCE_XTAL_32MHZ,
          LLCC68_LORA_DEFAULT_PREAMBLE_LENGTH, LLCC68_LORA_DEFAULT_HEADER,
          LLCC68_LORA_DEFAULT_CRC_TYPE, LLCC68_LORA_DEFAULT_INVERT_IQ,
          (uint8_t *)data, len, 0) != 0) {
    return 1;
  }

  return 0;
}

// 在 lora.c 中
void LoRa_RcvData(void) {
  if (LoRa_EnterRxMode() == 0) {
    ESP_LOGI(TAG, "LoRa 开启监听模式...");
  } else {
    ESP_LOGE(TAG, "LoRa 进入接收模式失败！");
  }
}
// 在 lora.c 中
uint8_t LoRa_EnterRxMode(void) {
  TXEN_LOW();
  RXEN_HIGH(); // 切换射频开关到接收路径

  // 1. 配置 IRQ：我们关注 接收完成、超时 和 CRC 错误
  if (llcc68_set_dio_irq_params(
          &gs_handle,
          LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT | LLCC68_IRQ_CRC_ERR,
          LLCC68_IRQ_RX_DONE | LLCC68_IRQ_TIMEOUT | LLCC68_IRQ_CRC_ERR, 0x0000,
          0x0000) != 0) {
    return 1;
  }

  // 2. 清除之前的状态
  llcc68_clear_irq_status(&gs_handle, 0x03FF);

  // 3. 进入连续接收模式 (或者单次接收)
  // 0xFFFFFF 表示连续接收，直到手动停止
  if (llcc68_set_rx(&gs_handle, 0xFFFFFF) != 0) {
    return 1;
  }

  return 0;
}