#include "IIC_SENSOR.h"
#include "app_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "IIC_SENSOR";

#define I2C_MASTER_FREQ_HZ 100000   // I2C 主机时钟频率 (100kHz)
#define I2C_MASTER_TX_BUF_DISABLE 0 // I2C 主机不需要发送缓存
#define I2C_MASTER_RX_BUF_DISABLE 0 // I2C 主机不需要接收缓存

// 全局静态变量，存储最新的传感器数据
static iic_sensor_data_t g_sensor_data = {0};
static SemaphoreHandle_t g_sensor_mutex = NULL;

/**
 * @brief 初始化 I2C 总线
 */
static esp_err_t iic_sensor_init(void) {
  int i2c_master_port = SENSOR_I2C_PORT;
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = SENSOR_I2C_SDA,
      .sda_pullup_en = GPIO_PULLUP_ENABLE, // 开启 SDA 上拉
      .scl_io_num = SENSOR_I2C_SCL,
      .scl_pullup_en = GPIO_PULLUP_ENABLE, // 开启 SCL 上拉
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };

  esp_err_t err = i2c_param_config(i2c_master_port, &conf);
  if (err != ESP_OK) {
    return err;
  }

  return i2c_driver_install(i2c_master_port, conf.mode,
                            I2C_MASTER_RX_BUF_DISABLE,
                            I2C_MASTER_TX_BUF_DISABLE, 0);
}

/**
 * @brief 读取 BH1750 光强数据
 */
static esp_err_t bh1750_read_lux(float *lux) {
  if (lux == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t data[2];
  uint8_t cmd = 0x20; // 单次高分辨率模式 1

  esp_err_t err = i2c_master_write_to_device(SENSOR_I2C_PORT, BH1750_ADDR, &cmd,
                                             1, 1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK)
    return err;

  vTaskDelay(pdMS_TO_TICKS(180));

  err = i2c_master_read_from_device(SENSOR_I2C_PORT, BH1750_ADDR, data, 2,
                                    1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK)
    return err;

  uint16_t raw_lux = (data[0] << 8) | data[1];
  *lux = (float)raw_lux / 1.2;
  return ESP_OK;
}

/**
 * @brief 读取 SHT30 温湿度数据
 */
static esp_err_t sht30_read_temp_humi(float *temp, float *hum) {
  if (temp == NULL || hum == NULL)
    return ESP_ERR_INVALID_ARG;
  uint8_t cmd[2] = {0x24, 0x00}; // 高重复性测量
  uint8_t data[6];

  esp_err_t err = i2c_master_write_to_device(SENSOR_I2C_PORT, SHT30_ADDR, cmd,
                                             2, 1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK)
    return err;

  vTaskDelay(pdMS_TO_TICKS(20));

  err = i2c_master_read_from_device(SENSOR_I2C_PORT, SHT30_ADDR, data, 6,
                                    1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK)
    return err;

  uint16_t raw_temp = (data[0] << 8) | data[1];
  uint16_t raw_hum = (data[3] << 8) | data[4];

  *temp = -45.0 + 175.0 * ((float)raw_temp / 65535.0);
  *hum = 100.0 * ((float)raw_hum / 65535.0);
  return ESP_OK;
}

/**
 * @brief 传感器采集后台任务
 */
static void iic_sensor_task(void *pvParameters) {
  float temp, hum, lux;
  ESP_LOGI(TAG, "Sensor task started");

  while (1) {
    // 读取 SHT30
    if (sht30_read_temp_humi(&temp, &hum) == ESP_OK) {
      xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
      g_sensor_data.temperature = temp;
      g_sensor_data.humidity = hum;
      xSemaphoreGive(g_sensor_mutex);
    } else {
      ESP_LOGW(TAG, "Failed to read SHT30");
    }

    // 读取 BH1750
    if (bh1750_read_lux(&lux) == ESP_OK) {
      xSemaphoreTake(g_sensor_mutex, portMAX_DELAY);
      g_sensor_data.lux = lux;
      xSemaphoreGive(g_sensor_mutex);
    } else {
      ESP_LOGW(TAG, "Failed to read BH1750");
    }

    ESP_LOGD(TAG, "Temp: %.2f, Hum: %.2f, Lux: %.2f", temp, hum, lux);

    // 每 2 秒采集一次
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

esp_err_t iic_sensor_task_start(void) {
  // 初始化 I2C
  esp_err_t err = iic_sensor_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed");
    return err;
  }

  // 创建互斥锁
  g_sensor_mutex = xSemaphoreCreateMutex();
  if (g_sensor_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  // 创建采集任务
  BaseType_t ret =
      xTaskCreate(iic_sensor_task, "iic_sensor_task", 4096, NULL, 5, NULL);
  if (ret != pdPASS) {
    return ESP_FAIL;
  }

  return ESP_OK;
}

void iic_sensor_get_data(iic_sensor_data_t *data) {
  // 检查传入指针和互斥锁是否有效
  if (data == NULL || g_sensor_mutex == NULL)
    return;

  /**
   * 【核心保护区】
   * 1. 获取互斥锁，如果锁被采集任务占用，则无限期等待(portMAX_DELAY)
   * 2. 成功拿到锁后，进行结构体赋值（数据拷贝）
   * 3. 释放互斥锁，允许采集任务继续更新数据
   */
  if (xSemaphoreTake(g_sensor_mutex, portMAX_DELAY) == pdTRUE) {
    *data = g_sensor_data;
    xSemaphoreGive(g_sensor_mutex);
  }
}
