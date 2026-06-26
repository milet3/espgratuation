#ifndef __IIC_SENSOR_H__
#define __IIC_SENSOR_H__

#include "esp_err.h"

typedef struct {
  float temperature;
  float humidity;
  float lux;
} iic_sensor_data_t;

esp_err_t iic_sensor_task_start(void);
esp_err_t iic_sensor_get_data(iic_sensor_data_t *data);

#endif // __IIC_SENSOR_H__
