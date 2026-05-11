#ifndef __SOIL_SENSOR_H__
#define __SOIL_SENSOR_H__

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief 土壤传感器数据结构体 (8合1)
 */
typedef struct {
  float temperature; // 温度 (℃)
  float humidity;    // 湿度 (%)
  float ec;          // 电导率 (us/cm)
  float salinity;    // 盐分 (mg/L)
  float nitrogen;    // 氮 (mg/kg)
  float phosphorus;  // 磷 (mg/kg)
  float potassium;   // 钾 (mg/kg)
  float ph;          // PH值
} soil_sensor_data_t;

/**
 * @brief 初始化土壤传感器串口
 *
 * @return esp_err_t ESP_OK 表示成功
 */
esp_err_t soil_sensor_init(void);

/**
 * @brief 触发一次传感器读取并获取数据
 *
 * @param data 指向存储结果的结构体指针
 * @return esp_err_t ESP_OK 表示成功读取
 */
esp_err_t soil_sensor_read_data(soil_sensor_data_t *data);

#endif // __SOIL_SENSOR_H__
