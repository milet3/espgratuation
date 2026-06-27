/*-------------------------------------------------*/
/*                                                 */
/*   实现存储功能的源文件 (基于 ESP32 NVS)         */
/*   将原 24C02 EEPROM 的功能重构为使用内部 Flash  */
/*                                                 */
/*-------------------------------------------------*/

#include "bsp_storage.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h" // 引入全局的结构体定义

static const char *TAG = "BSP_NVS";

/*-------------------------------------------------*/
/*函数名：初始化 NVS                               */
/*返回值：esp_err_t                                */
/*-------------------------------------------------*/
esp_err_t EEprom_Init(void)
{
    // 初始化 NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS 分区被截断需要擦除并重新初始化
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGD(TAG, "NVS 初始化完成");
    return err;
}

/*-------------------------------------------------*/
/*函数名：从NVS指定的键读取指定数量的数据            */
/*参  数：key: 需要读取数据的键名                    */
/*参  数：data:保存读取数据的缓冲区                */
/*参  数：len:要读取的数据长度                     */
/*返回值：无                                       */
/*-------------------------------------------------*/
void EEprom_ReadData(const char* key, void *data, size_t len)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // 打开
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 句柄失败: %s", esp_err_to_name(err));
        return;
    }
    
    // 读取
    size_t required_size = len;
    err = nvs_get_blob(my_handle, key, data, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "从 NVS 读取数据失败: %s", esp_err_to_name(err));
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "键 '%s' 尚未初始化，使用默认值", key);
        memset(data, 0, len); // 如果没有找到，清空缓冲区
    } else {
        ESP_LOGD(TAG, "已从 NVS 读取键 '%s'", key);
    }

    // 关闭
    nvs_close(my_handle);
}

/*-------------------------------------------------*/
/*函数名：向NVS指定的键写入数据                      */
/*参  数：key：指定写入的键名                        */
/*参  数：data：需要写入的数据                       */
/*参  数：len：写入多少数据                        */
/*返回值：无                                       */
/*-------------------------------------------------*/
void EEprom_WriteData(const char* key, void *data, size_t len)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // 打开
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 句柄失败: %s", esp_err_to_name(err));
        return;
    }

    // 写入
    err = nvs_set_blob(my_handle, key, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入 NVS 数据失败");
    } else {
        ESP_LOGD(TAG, "已写入 NVS 键 '%s'", key);
    }

    // 提交写入的值
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "提交 NVS 更新失败");
    }

    // 关闭
    nvs_close(my_handle);
}

/*-------------------------------------------------*/
/*函数名：从EEprom(NVS)读取所有证书参数信息          */
/*参  数：无                                       */
/*返回值：无                                       */
/*-------------------------------------------------*/
void EEprom_ReadInfo(void)
{
    memset(&info, 0, INFO_STRUCT_LEN);                      // 清空结构体
    EEprom_ReadData("info_data", &info, INFO_STRUCT_LEN);   // 从键 "info_data" 开始，读取保存的数据
    
    // 注意：这里需要替换 u1_printf 为 ESP_LOGI，因为 ESP32 不使用 u1_printf
    ESP_LOGI(TAG, "网关当前版本号：%s", info.Version[0]);    // 串口输出提示信息
    ESP_LOGI(TAG, "子设备当前版本号：%s", info.Version[1]);  // 串口输出提示信息
}


/*-------------------------------------------------*/
/* Boot Loop Detection - NVS keys                  */
/*-------------------------------------------------*/
#define BOOT_COUNT_KEY     "boot_cnt"
#define BOOT_WAS_STABLE_KEY "boot_stbl"

uint32_t boot_loop_get_count(void) {
    uint32_t count = 0;
    EEprom_ReadData(BOOT_COUNT_KEY, &count, sizeof(count));
    return count;
}

void boot_loop_set_count(uint32_t count) {
    EEprom_WriteData(BOOT_COUNT_KEY, &count, sizeof(count));
}

uint32_t boot_loop_get_was_stable(void) {
    uint32_t was_stable = 0;
    EEprom_ReadData(BOOT_WAS_STABLE_KEY, &was_stable, sizeof(was_stable));
    return was_stable;
}

void boot_loop_set_was_stable(uint32_t was_stable) {
    EEprom_WriteData(BOOT_WAS_STABLE_KEY, &was_stable, sizeof(was_stable));
}

/*-------------------------------------------------*/
/* Factory Reset Functions                        */
/*-------------------------------------------------*/
#define FACTORY_RESET_KEY "factory_rst"

void factory_reset(void) {
    ESP_LOGW(TAG, "=== FACTORY RESET TRIGGERED ===");
    ESP_LOGW(TAG, "Erasing all NVS data...");
    nvs_flash_erase();
    ESP_LOGW(TAG, "NVS erased. Rebooting in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

void partial_factory_reset(void) {
    ESP_LOGW(TAG, "=== PARTIAL FACTORY RESET ===");
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }
    nvs_erase_key(handle, "wifi_creds");
    nvs_erase_key(handle, "soil_calib");
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGW(TAG, "Partial reset done. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

void factory_reset_set_pending(void) {
    uint32_t flag = 1;
    EEprom_WriteData(FACTORY_RESET_KEY, &flag, sizeof(flag));
    ESP_LOGW(TAG, "Factory reset flag set. Will execute on next reboot.");
}

bool factory_reset_is_pending(void) {
    uint32_t flag = 0;
    EEprom_ReadData(FACTORY_RESET_KEY, &flag, sizeof(flag));
    return (flag == 1);
}
