/*-------------------------------------------------*/
/*                                                 */
/*   实现存储功能的头文件 (基于 ESP32 NVS)         */
/*   将原 24C02 EEPROM 的功能重构为使用内部 Flash  */
/*                                                 */
/*-------------------------------------------------*/

#ifndef __BSP_STORAGE_H
#define __BSP_STORAGE_H

#include "stdint.h"
#include <stdbool.h>
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



/**
 * @brief 获取 boot loop 计数器（连续短命启动次数）
 * @return 当前 boot_count 值（首次启动返回 0）
 */
uint32_t boot_loop_get_count(void);

/**
 * @brief 设置 boot loop 计数器
 * @param count 新的计数值
 */
void boot_loop_set_count(uint32_t count);

/**
 * @brief 获取上次启动是否稳定运行（存活 > 60s）
 * @return 0 = 上次启动未稳定，1 = 上次启动已稳定
 */
uint32_t boot_loop_get_was_stable(void);

/**
 * @brief 设置稳定标志
 * @param was_stable 0 = 未稳定，1 = 已稳定
 */
void boot_loop_set_was_stable(uint32_t was_stable);
/**
 * @brief 工厂重置：擦除所有 NVS 数据并立即重启
 */
void factory_reset(void);

/**
 * @brief 半工厂重置：仅擦除 WiFi 凭证和传感器校准
 */
void partial_factory_reset(void);

/**
 * @brief 设置出厂重置标志（由 MQTT/按键触发时调用）
 */
void factory_reset_set_pending(void);

/**
 * @brief 检查是否有待执行的出厂重置
 * @return true 有待执行的重置
 */
bool factory_reset_is_pending(void);


/**
 * @brief 获取 boot loop 计数器（连续短命启动次数）
 * @return 当前 boot_count 值（首次启动返回 0）
 */
uint32_t boot_loop_get_count(void);

/**
 * @brief 设置 boot loop 计数器
 * @param count 新的计数值
 */
void boot_loop_set_count(uint32_t count);

/**
 * @brief 获取上次启动是否稳定运行（存活 > 60s）
 * @return 0 = 上次启动未稳定，1 = 上次启动已稳定
 */
uint32_t boot_loop_get_was_stable(void);

/**
 * @brief 设置稳定标志
 * @param was_stable 0 = 未稳定，1 = 已稳定
 */
void boot_loop_set_was_stable(uint32_t was_stable);
/**
 * @brief 工厂重置：擦除所有 NVS 数据并立即重启
 */
void factory_reset(void);

/**
 * @brief 半工厂重置：仅擦除 WiFi 凭证和传感器校准
 */
void partial_factory_reset(void);

/**
 * @brief 设置出厂重置标志（由 MQTT/按键触发时调用）
 */
void factory_reset_set_pending(void);

/**
 * @brief 检查是否有待执行的出厂重置
 * @return true 有待执行的重置
 */
bool factory_reset_is_pending(void);
#endif // __BSP_STORAGE_H
