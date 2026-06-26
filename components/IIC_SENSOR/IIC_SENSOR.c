#include "IIC_SENSOR.h"
#include "app_config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdbool.h>

static const char *TAG = "IIC_SENSOR";

#define I2C_MASTER_FREQ_HZ 50000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

static iic_sensor_data_t g_sensor_data = {0};
static SemaphoreHandle_t g_sensor_mutex = NULL;
static bool g_sht30_ready = false;
static bool g_bh1750_ready = false;
static uint8_t g_bh1750_runtime_addr = 0;

static uint8_t bh1750_normalize_addr(uint8_t addr) {
  if (addr == 0x23 || addr == 0x5C) {
    return addr;
  }

  uint8_t shifted_addr = (uint8_t)(addr >> 1);
  if (shifted_addr == 0x23 || shifted_addr == 0x5C) {
    static bool warned = false;
    if (!warned) {
      ESP_LOGW(TAG,
               "BH1750_ADDR=0x%02X looks like an 8-bit address, using 7-bit "
               "0x%02X",
               addr, shifted_addr);
      warned = true;
    }
    return shifted_addr;
  }

  return addr;
}

static esp_err_t iic_sensor_init(void) {
  const int i2c_master_port = SENSOR_I2C_PORT;
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = SENSOR_I2C_SDA,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_io_num = SENSOR_I2C_SCL,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };

  ESP_LOGI(TAG, "Initializing I2C master on SDA:%d SCL:%d", SENSOR_I2C_SDA,
           SENSOR_I2C_SCL);

  esp_err_t err = i2c_param_config(i2c_master_port, &conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
    return err;
  }

  err = i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE,
                           I2C_MASTER_TX_BUF_DISABLE, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
    return err;
  }

  return ESP_OK;
}

static esp_err_t bh1750_read_lux_once(uint8_t addr, float *lux) {
  if (lux == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t data[2];
  uint8_t cmd = 0x20;

  esp_err_t err = i2c_master_write_to_device(SENSOR_I2C_PORT, addr, &cmd, 1,
                                             1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "BH1750 write cmd failed on 0x%02X: %s", addr,
             esp_err_to_name(err));
    return err;
  }

  vTaskDelay(pdMS_TO_TICKS(180));

  err = i2c_master_read_from_device(SENSOR_I2C_PORT, addr, data, 2,
                                    1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "BH1750 read failed on 0x%02X: %s", addr,
             esp_err_to_name(err));
    return err;
  }

  uint16_t raw_lux = (uint16_t)((data[0] << 8) | data[1]);
  *lux = (float)raw_lux / 1.2f;
  return ESP_OK;
}

static esp_err_t bh1750_read_lux(float *lux) {
  if (lux == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t candidates[3] = {bh1750_normalize_addr(BH1750_ADDR), 0x23, 0x5C};
  esp_err_t last_err = ESP_FAIL;

  if (g_bh1750_runtime_addr != 0) {
    last_err = bh1750_read_lux_once(g_bh1750_runtime_addr, lux);
    if (last_err == ESP_OK) {
      return ESP_OK;
    }
  }

  for (int i = 0; i < 3; ++i) {
    bool duplicate = false;
    for (int j = 0; j < i; ++j) {
      if (candidates[i] == candidates[j]) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }

    last_err = bh1750_read_lux_once(candidates[i], lux);
    if (last_err == ESP_OK) {
      if (g_bh1750_runtime_addr != candidates[i]) {
        ESP_LOGI(TAG, "BH1750 detected at I2C address 0x%02X", candidates[i]);
        g_bh1750_runtime_addr = candidates[i];
      }
      return ESP_OK;
    }
  }

  return last_err;
}

static esp_err_t sht30_read_temp_humi(float *temp, float *hum) {
  if (temp == NULL || hum == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t cmd[2] = {0x24, 0x00};
  uint8_t data[6];

  esp_err_t err = i2c_master_write_to_device(SENSOR_I2C_PORT, SHT30_ADDR, cmd, 2,
                                             1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "SHT30 write cmd failed: %s", esp_err_to_name(err));
    return err;
  }

  vTaskDelay(pdMS_TO_TICKS(20));

  err = i2c_master_read_from_device(SENSOR_I2C_PORT, SHT30_ADDR, data, 6,
                                    1000 / portTICK_PERIOD_MS);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "SHT30 read data failed: %s", esp_err_to_name(err));
    return err;
  }

  uint16_t raw_temp = (uint16_t)((data[0] << 8) | data[1]);
  uint16_t raw_hum = (uint16_t)((data[3] << 8) | data[4]);

  *temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
  *hum = 100.0f * ((float)raw_hum / 65535.0f);
  return ESP_OK;
}

static void iic_sensor_task(void *pvParameters) {
  (void)pvParameters;

  float temp = 0.0f;
  float hum = 0.0f;
  float lux = 0.0f;
  uint32_t last_sht_fail_log = 0;
  uint32_t last_bh_fail_log = 0;

  ESP_LOGI(TAG, "Sensor task started");

  while (1) {
    if (sht30_read_temp_humi(&temp, &hum) == ESP_OK) {
      if (xSemaphoreTake(g_sensor_mutex, portMAX_DELAY) == pdTRUE) {
        g_sensor_data.temperature = temp;
        g_sensor_data.humidity = hum;
        g_sht30_ready = true;
        xSemaphoreGive(g_sensor_mutex);
      }
    } else if (esp_log_timestamp() - last_sht_fail_log > 30000U) {
      ESP_LOGW(TAG, "Failed to read SHT30 (silencing logs for 30s)");
      last_sht_fail_log = esp_log_timestamp();
    }

    if (bh1750_read_lux(&lux) == ESP_OK) {
      if (xSemaphoreTake(g_sensor_mutex, portMAX_DELAY) == pdTRUE) {
        g_sensor_data.lux = lux;
        g_bh1750_ready = true;
        xSemaphoreGive(g_sensor_mutex);
      }
    } else if (esp_log_timestamp() - last_bh_fail_log > 30000U) {
      ESP_LOGW(TAG, "Failed to read BH1750 (silencing logs for 30s)");
      last_bh_fail_log = esp_log_timestamp();
    }

    ESP_LOGD(TAG, "Temp: %.2f, Hum: %.2f, Lux: %.2f", temp, hum, lux);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

esp_err_t iic_sensor_task_start(void) {
  esp_err_t err = iic_sensor_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed");
    return err;
  }

  g_sensor_mutex = xSemaphoreCreateMutex();
  if (g_sensor_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  BaseType_t ret =
      xTaskCreate(iic_sensor_task, "iic_sensor_task", 4096, NULL, 5, NULL);
  if (ret != pdPASS) {
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t iic_sensor_get_data(iic_sensor_data_t *data) {
  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (g_sensor_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(g_sensor_mutex, portMAX_DELAY) == pdTRUE) {
    *data = g_sensor_data;
    bool temp_humi_ready = g_sht30_ready;
    bool lux_ready = g_bh1750_ready;
    if (!lux_ready) {
      data->lux = -1.0f;
    }
    xSemaphoreGive(g_sensor_mutex);
    return temp_humi_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
  }

  return ESP_FAIL;
}
