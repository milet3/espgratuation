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
#include "app_config.h" // 引入全局的结构体定义

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
    ESP_LOGI(TAG, "NVS initialized successfully");
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
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }
    
    // 读取
    size_t required_size = len;
    err = nvs_get_blob(my_handle, key, data, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Error (%s) reading from NVS!", esp_err_to_name(err));
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "The value is not initialized yet!");
        memset(data, 0, len); // 如果没有找到，清空缓冲区
    } else {
        ESP_LOGI(TAG, "Read data from NVS successfully");
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
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    // 写入
    err = nvs_set_blob(my_handle, key, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write data to NVS!");
    } else {
        ESP_LOGI(TAG, "Write data to NVS successfully");
    }

    // 提交写入的值
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit updates in NVS!");
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
