#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#include "driver/gpio.h"
#include <stdint.h>

#define LED_GW001_LED_PIN GPIO_NUM_3 //

// LoRa MW1268 模块 (UART 接口)
#define LORA_UART_TX GPIO_NUM_16
#define LORA_UART_RX GPIO_NUM_15
#define LORA_MD0_PIN GPIO_NUM_18
#define LORA_AUX_PIN GPIO_NUM_17
#ifndef LORA_UART_PORT
#define LORA_UART_PORT                                                         \
  UART_NUM_2 // 调试阶段：从 UART0 搬到 UART2，避免干扰系统日志打印
#endif

// LoRa LLCC68 模块 (SPI 接口 - 已废弃)
/*
#define LORA_SPI_SCK GPIO_NUM_46
#define LORA_SPI_MISO GPIO_NUM_38
#define LORA_SPI_MOSI GPIO_NUM_3
#define LORA_SPI_CS -1
#define LORA_RESET_PIN GPIO_NUM_39
#define LORA_BUSY_PIN GPIO_NUM_40
#define LORA_DIO1_PIN GPIO_NUM_1
#define LORA_TXEN_PIN GPIO_NUM_2
#define LORA_RXEN_PIN -1
*/

// 4G EC800 模块 (UART 接口 + 控制)
#define CAT1_TX_PIN GPIO_NUM_5 // 对应原理图 IO18 (避开调试串口)
#define CAT1_RX_PIN GPIO_NUM_4 // 对应原理图 IO17 (避开调试串口)
#define CAT1_PORT                                                              \
  UART_NUM_0 // 调试阶段：从 UART2 搬到 UART0（或在 WiFi 模式下不使用）
#define CAT1_POWER_STATE_PIN GPIO_NUM_6 //
#define CAT1_POWER_STA_PIN -1           // 原理图未引出反馈引脚
#define CAT1_NET_STA_PIN -1             // 原理图未引出反馈引脚

// 传感器 I2C 接口 (BH1750 光强 & SHT30 温湿度)
#define SENSOR_I2C_SDA GPIO_NUM_48
#define SENSOR_I2C_SCL GPIO_NUM_47
#define SENSOR_I2C_PORT I2C_NUM_0
#define BH1750_ADDR 0x46
#define SHT30_ADDR 0x44

// 土壤传感器 (UART 接口)
#define SOIL_TX_PIN GPIO_NUM_12
#define SOIL_RX_PIN GPIO_NUM_11
#define SOIL_UART_POWER_PIN GPIO_NUM_10
#define SOIL_UART_GND_PIN GPIO_NUM_9
#define SOIL_UART_PORT UART_NUM_1
#define SOIL_UART_BAUDRATE 9600

// 其他功能

/* USER CODE BEGIN Private defines */
#define SUN_NUMBER 3 // 使用几个节点板 (1网关+3子节点，总共4个元素)

#define MQTT_SERVER "mqtts.heclouds.com" // 恢复域名连接，配合手动 DNS 设置
#define MQTT_PORT 1883                   // MQTT服务器端口号

#define OTA_SERVER "iot-api.heclouds.com" // OTA服务器域名
#define OTA_PORT 80                       // OTA服务器端口

#define UNIX "1861891199"          // token运算时的过期UNIX时间戳
#define CURRENT_FW_VERSION "1.0.0" // 当前固件版本号 (用于 OTA 匹配)
#define Accesskey "0b5b4ffaac3847ca9fc6bf1d71ac9b9e" // 用户Accesskey
#define USERID "519184"                              // 用户ID
#define GATEWAY_VERSION "1.0.0"                      // 网关固件版本号

#define GW_PRODUCTID "C9fKaO0V7f" // 网关产品ID
#define GW_DEVICENAME "GW001"     // 网关设备名称
#define GW_DEVICESECRET                                                        \
  "UG95UU1MS002eUFIYmFKdzRGc1JKRmxmWE9IelAxUnM=" // 网关设备密钥
#define SUB_PRODUCTID "lU2R784idd"               // 子设备产品ID
#define SUB1_PDEVICENAME "D001"                  // 子设备1设备名称
#define SUB2_PDEVICENAME "D002"                  // 子设备2设备名称
#define SUB3_PDEVICENAME "D003"                  // 子设备3设备名称

#define ATTRIBUTE1                                                             \
  "PestAlarm" // 功能属性1标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE2                                                             \
  "PowerSwitch_2" // 功能属性2标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE3                                                             \
  "PowerSwitch_3" // 功能属性3标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE4                                                             \
  "PowerSwitch_4" // 功能属性4标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE5                                                             \
  "temperature" // 功能属性5标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE6                                                             \
  "humidity" // 功能属性6标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE7                                                             \
  "lightlux" // 功能属性7标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE8                                                             \
  "ADC_CH1" // 功能属性8标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE9                                                             \
  "ADC_CH2" // 功能属性9标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE10                                                            \
  "ADC_CH3" // 功能属性10标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
#define ATTRIBUTE_SOIL_TEMP "soil_temp" // 土壤温度
#define ATTRIBUTE_SOIL_HUMI "soil_humi" // 土壤水分
#define ATTRIBUTE_SOIL_EC "soil_ec"     // 土壤电导率
#define ATTRIBUTE_SOIL_N "soil_n"       // 土壤氮
#define ATTRIBUTE_SOIL_P "soil_p"       // 土壤磷
#define ATTRIBUTE_SOIL_K "soil_k"       // 土壤钾
#define ATTRIBUTE_LIGHTLUX "lightlux_D001"
#define ATTRIBUTE_TEMP "temperature_D001"
#define ATTRIBUTE_HUMI "humidity_D001"
#define ATTRIBUTE_FIRMWARE_VER                                                 \
  "firmware_version" // 固件版本属性标识符 (直连设备 OTA 关键)

/*---------------------------------------------------------------*/
/*-------------------用于各种系统参数的结构体--------------------*/
/*---------------------------------------------------------------*/
typedef struct {
  float temperature;
  float humidity;
  float lightlux;
} node_sensor_data_t;

typedef struct {
  uint32_t SysEventFlag; // 发生各种事件的标志变量
  uint32_t PingTimer;    // 用于记录发送PING数据包的计时器
  node_sensor_data_t last_node_data; // 缓存子节点最新数据
} Sys_CB;
#define SYS_STRUCT_LEN sizeof(Sys_CB) // 用于各种系统参数的 Sys_CB结构体 长度

//---------------------------------------------------------------//
//------------------启动信息字节，表示含义-----------------------//
//---------------------------------------------------------------//
#define BOOT_STA_O                                                             \
  0x0AA0C00C // Info_CB结构体内OTA_flag成员等于此值时，表示需要OTA更新应用程序

/*---------------------------------------------------------------*/
/*----------------------EEPROM内保存信息的结构体-----------------*/
/*---------------------------------------------------------------*/
#define VERSION_LEN 6 // 版本号长度
typedef struct {
  uint8_t Version[2][VERSION_LEN + 2]; // 版本号缓冲区 0：网关 1：子设备
  uint32_t OTA_firelen[2]; // OTA固件大小  0：网关 1：子设备
  uint32_t OTA_flag;       // 是否需要OTA的标识
} Info_CB;
#define INFO_STRUCT_LEN sizeof(Info_CB) // EEEPROM内保存信息的结构体 长度

/*---------------------------------------------------------------*/
/*-----------------------系统事件发生标志定义--------------------*/
/*---------------------------------------------------------------*/
#define CONNECT_MQTT ((uint32_t)0x00000001) // 连接上MQTT服务器事件
#define CONNECT_OTA ((uint32_t)0x00000002)  // 连接上OTA服务器事件
#define CONNECT_WIFI ((uint32_t)0x00000004) // WiFi模块连接上服务器事件
#define CONNECT_CAT1 ((uint32_t)0x00000008) // 4G Cat1模块连接上服务器事件
#define CONNECT_PING ((uint32_t)0x00000010) // 需要发送MQTT协议PING保活报文事件
#define OTA_EVENT ((uint32_t)0x00000020)   // 需要进行OTA操作事件
#define OTA_RUNNING ((uint32_t)0x00000040) // 正在进行OTA下载标志位
#define SUB_ONLINE_READY ((uint32_t)0x00000080) // 子设备已发送上线报备标志位
#define SUB_LORA_CONFIRMED                                                     \
  ((uint32_t)0x00000100) // 子设备 LoRa 通信已确认标志位

/* Event Group bit definitions */
#define EVG_NET_READY (0x0001U)  // 网络连接上服务器事件
#define EVG_MQTT_READY (0x0002U) // MQTT服务器连接上事件

//------------各种外部变量声明，便于其他源文件调用变量-----------//
extern Sys_CB SysCB; // 外部变量声明，用于各种系统参数的结构体
extern char DeviceNameBuff[SUN_NUMBER + 1][64]; // 外部变量声明，设备名称数组
extern char ProductIdBuff[SUN_NUMBER + 1][64]; // 外部变量声明，产品ID数组
extern Info_CB info; // 外部变量声明，EEPROM内保存信息的结构体

#endif // __APP_CONFIG_H__