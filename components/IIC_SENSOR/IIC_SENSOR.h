#ifndef __IIC_SENSOR_H__
#define __IIC_SENSOR_H__

#include "esp_err.h"

/**
 * @brief 传感器数据结构体
 */
typedef struct {
  float temperature; // 温度
  float humidity;    // 湿度
  float lux;         // 光照强度
} iic_sensor_data_t;

/**
 * @brief 启动传感器采集任务
 *
 * @return esp_err_t ESP_OK 表示成功
 */
esp_err_t iic_sensor_task_start(void);

/**
 * @brief 获取最新的传感器数据（线程安全）
 *
 * @note 此函数使用互斥锁保护，确保读取到的数据是一次完整采集的结果，
 *       避免在多任务并发访问时出现数据撕裂或不一致。
 *
 * @param data 指向存储结果的结构体指针，函数会将最新的温湿度和光强拷贝至此
 */
void iic_sensor_get_data(iic_sensor_data_t *data);

#endif // __IIC_SENSOR_H__
