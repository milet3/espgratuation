/*-------------------------------------------------*/
/*                                                 */
/*   实现存储功能的头文件 (基于 ESP32 NVS)         */
/*   将原 24C02 EEPROM 的功能重构为使用内部 Flash  */
/*                                                 */
/*-------------------------------------------------*/

#ifndef __BSP_STORAGE_H
#define __BSP_STORAGE_H

#include "stdint.h"
#include "esp_err.h"

// 命名空间定义，NVS中存储数据需要一个命名空间
#define NVS_NAMESPACE "storage"

/**
 * @brief 初始化 NVS (Non-Volatile Storage)
 * 
 * @return esp_err_t ESP_OK 成功, 其他失败
 */
esp_err_t EEprom_Init(void);

/**
 * @brief 从 NVS 中读取指定键名的数据
 * 
 * @param key 需要读取数据的键名 (字符串)
 * @param data 保存读取数据的缓冲区指针
 * @param len 要读取的数据长度
 */
void EEprom_ReadData(const char* key, void *data, size_t len);

/**
 * @brief 向 NVS 中写入指定键名的数据
 * 
 * @param key 需要写入数据的键名 (字符串)
 * @param data 需要写入的数据指针
 * @param len 要写入的数据长度
 */
void EEprom_WriteData(const char* key, void *data, size_t len);

/**
 * @brief 从 NVS 读取所有证书参数信息 (业务逻辑)
 */
void EEprom_ReadInfo(void);

#endif // __BSP_STORAGE_H
