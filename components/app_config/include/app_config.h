#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#include <stdint.h>

// 系统电源控制
#define LORA_POWER_PIN GPIO_NUM_6
#define EM_POWER_PIN GPIO_NUM_15 // 原 IO7 被 LED1 占用，改为 IO15

// Maix Bit K210 模块 (UART 接口 + 控制)
#define K210_TX_PIN GPIO_NUM_13   // 接 K210_RX
#define K210_RX_PIN GPIO_NUM_14   // 接 K210_TX
#define K210_RST_PIN GPIO_NUM_11  // 原 IO47 被 LED5 占用，改为 IO11
#define K210_BOOT_PIN GPIO_NUM_21 // K210_IO0 (配合 RST 进入下载模式)
#define K210_INT_PIN GPIO_NUM_10  // K210_PIN6 (视觉识别中断监听)

// LoRa LLCC68 模块 (SPI 接口)
#define LORA_SPI_SCK GPIO_NUM_46
#define LORA_SPI_MISO GPIO_NUM_38
#define LORA_SPI_MOSI GPIO_NUM_3
#define LORA_SPI_CS GPIO_NUM_9
#define LORA_RESET_PIN GPIO_NUM_39
#define LORA_BUSY_PIN GPIO_NUM_40
#define LORA_DIO1_PIN GPIO_NUM_1
#define LORA_TXEN_PIN GPIO_NUM_2
#define LORA_RXEN_PIN GPIO_NUM_12

// 4G EC800 模块 (UART 接口 + 控制)
#define CAT1_TX_PIN GPIO_NUM_17
#define CAT1_RX_PIN GPIO_NUM_16
#define CAT1_POWER_PIN GPIO_NUM_4     // PWRKEY
#define CAT1_POWER_STA_PIN GPIO_NUM_5 // STATUS
#define CAT1_NET_STA_PIN GPIO_NUM_15  // NETLIGHT

// 传感器 I2C 接口 (BH1750 光强 & SHT30 温湿度)
#define SENSOR_I2C_SDA GPIO_NUM_48
#define SENSOR_I2C_SCL GPIO_NUM_45
#define SENSOR_I2C_PORT I2C_NUM_0
#define BH1750_ADDR 0x23
#define SHT30_ADDR 0x44

// 土壤传感器 (UART 接口)
#define SOIL_TX_PIN GPIO_NUM_41
#define SOIL_RX_PIN GPIO_NUM_42
#define SOIL_UART_PORT UART_NUM_1

// 系统指示灯 (匹配原理图 LED1, LED4, LED5)
#define LED_RUN_PIN GPIO_NUM_6   // 系统运行指示灯 (LED1)
#define LED_LORA_PIN GPIO_NUM_18 // LoRa 通讯指示灯 (LED4)
#define LED_NET_PIN GPIO_NUM_47  // 网络状态指示灯 (LED5)

// 其他功能

/* USER CODE BEGIN Private defines */
#define SUN_NUMBER 3 // 使用几个节点板

#define MQTT_SERVER "mqtts.heclouds.com" // MQTT服务器域名
#define MQTT_PORT 1883                   // MQTT服务器端口号

#define OTA_SERVER "iot-api.heclouds.com" // OTA服务器域名
#define OTA_PORT 80                       // OTA服务器端口

#define UNIX "1861891199" // token运算时的过期UNIX时间戳
#define Accesskey                                                              \
  "jT9gyXrxk+LkJlxb/nEkL+nAZiD+5EacXAyLb9eskQg/"                               \
  "MMI4qXcNNgZUUU1+rlwV97S1ZG6Hyq3prCt5MvoC3g==" // 用户Accesskey
#define USERID "27124"                           // 用户ID

#define GW_PRODUCTID "Po28fBqti6" // 网关产品ID
#define GW_DEVICENAME "GW001"     // 网关设备名称
#define GW_DEVICESECRET                                                        \
  "RmN5a0pGdEdtQzM0NGpxVTZsNVRONkllMGxsb2NwcW0=" // 网关设备密钥
#define SUB_PRODUCTID "wEzuaN5K2L"               // 子设备产品ID
#define SUB1_PDEVICENAME "D001"                  // 子设备1设备名称
#define SUB2_PDEVICENAME "D002"                  // 子设备2设备名称
#define SUB3_PDEVICENAME "D003"                  // 子设备3设备名称

#define ATTRIBUTE1                                                             \
  "PowerSwitch_1" // 功能属性1标识符，标识符名称必须和服务器后台设置的完全一样，大小写也必须一样
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

/*---------------------------------------------------------------*/
/*-------------------用于各种系统参数的结构体--------------------*/
/*---------------------------------------------------------------*/
typedef struct {
  uint32_t SysEventFlag; // 发生各种事件的标志变量
  uint32_t PingTimer;    // 用于记录发送PING数据包的计时器
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
#define OTA_EVENT ((uint32_t)0x00000020) // 需要进行OTA操作事件

/* Event Group bit definitions */
#define EVG_NET_READY (0x0001U)  // 网络连接上服务器事件
#define EVG_MQTT_READY (0x0002U) // MQTT服务器连接上事件

//------------各种外部变量声明，便于其他源文件调用变量-----------//
extern Sys_CB SysCB; // 外部变量声明，用于各种系统参数的结构体
extern char DeviceNameBuff[SUN_NUMBER + 1][64]; // 外部变量声明，设备名称数组
extern char PorductIdBuff[SUN_NUMBER + 1][64]; // 外部变量声明，产品ID数组
extern Info_CB info; // 外部变量声明，EEPROM内保存信息的结构体

#endif // __APP_CONFIG_H__