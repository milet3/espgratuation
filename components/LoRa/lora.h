/*-------------------------------------------------*/
/*                                                 */
/*            操作LoRa模块功能的头文件             */
/*                                                 */
/*-------------------------------------------------*/

#ifndef __LORA_H
#define __LORA_H

#include "app_config.h"
#include "driver/gpio.h"
#include <stdint.h>

// ==========================================
// 需要根据 ESP32 的实际接线修改以下引脚宏定义
// ==========================================
// M0 和 M1 引脚用于控制 LoRa 的工作模式 (仅限 UART 模块)
// 在当前 SPI 驱动中已不再使用这些宏

// 使用 app_config.h 中的定义
#define LORA_AUX_PIN LORA_BUSY_PIN
#define LORA_PWR_PIN LORA_POWER_PIN

// SUN_NUMBER 和 VERSION_LEN 已经在 app_config.h 中定义
// #define SUN_NUMBER     10
// #define VERSION_LEN    5

typedef struct {
  uint8_t LoRa_AddrH; // 模块地址高字节	      地址0x00
  uint8_t LoRa_AddrL; // 模块地址低字节	      地址0x01
  uint8_t LoRa_NetID; // 模块网络地址            地址0x02

  uint8_t LoRa_Baudrate;    // 模块串口波特率          地址0x03
  uint8_t LoRa_UartMode;    // 模块串口工作模式        地址0x03
  uint8_t LoRa_airvelocity; // 模块空中速率            地址0x03

  uint8_t LoRa_DataLen; // 数据分包大小            地址0x04
  uint8_t LoRa_Rssi;    // 环境噪声使能            地址0x04
  uint8_t LoRa_Soft;    // 模块工作模式软件切换    地址0x04
  uint8_t LoRa_TxPower; // 模块发射功率            地址0x04

  uint8_t LoRa_CH; // 模块信道                地址0x05

  uint8_t LoRa_RssiByte; // Rssi字节功能            地址0x06
  uint8_t LoRa_DateMode; // 模块数据传输模式        地址0x06
  uint8_t LoRa_Relay;    // 模块中继功能            地址0x06
  uint8_t LoRa_LBT;      // 模块LBT监听功能         地址0x06
  uint8_t LoRa_WORmode;  // 模块WOR模式下工作方式   地址0x06
  uint8_t LoRa_WORcycle; // 模块WOR模式下周期时间   地址0x06

  uint8_t LoRa_KeyH; // 加密秘钥高字节          地址0x07
  uint8_t LoRa_KeyL; // 加密秘钥低字节          地址0x08

} LoRaParameter;

typedef struct {
  uint8_t sta; // LoRa模块状态变量 0：配置状态   1：数据传输状态
  uint32_t timer;                  // 读取节点传感器数据计时变量
  uint32_t counter;                // 读取节点的计数变量
  uint32_t online[SUN_NUMBER + 1]; // 0：下线状态  1：上线状态
  uint32_t timeout
      [SUN_NUMBER +
       1]; // LoRa读取子设备数据累计次数变量，超过次数不回复数据，可认定子设备不在线了
  uint32_t SW_Sta[SUN_NUMBER + 1]; // 各个节点板开关状态
  uint8_t SubVer[SUN_NUMBER + 1][VERSION_LEN + 1]; // 各个节点板版本号缓冲区
  uint32_t Ota; // 非0时：哪个从机子设备开始OTA升级  0：子设备无OTA
  uint32_t OtaNum;       // 子设备OTA升级总共次数
  uint32_t OtaCounter;   // 子设备OTA升级当前次数
  uint8_t OTA_Buff[300]; // 子设备OTA数据缓冲区
} LoRaCB;                // LoRa模块控制结构体

/*
#define LoRa_MODE0                                                             \
  gpio_set_level(LORA_M0_PIN, 0);                                              \
  gpio_set_level(LORA_M1_PIN, 0) // LoRa模块 模式0
#define LoRa_MODE1                                                             \
  gpio_set_level(LORA_M0_PIN, 1);                                              \
  gpio_set_level(LORA_M1_PIN, 0) // LoRa模块 模式1
#define LoRa_MODE2                                                             \
  gpio_set_level(LORA_M0_PIN, 0);                                              \
  gpio_set_level(LORA_M1_PIN, 1) // LoRa模块 模式2
#define LoRa_MODE3                                                             \
  gpio_set_level(LORA_M0_PIN, 1);                                              \
  gpio_set_level(LORA_M1_PIN, 1) // LoRa模块 模式3
*/

#define LoRa_AUX gpio_get_level(LORA_AUX_PIN) // 读取电平状态,判断模块状态
#define LoRa_PowerON                                                           \
  gpio_set_level(LORA_PWR_PIN, 1) // 控制loRa模块供电,高电平ON
#define LoRa_PowerOFF                                                          \
  gpio_set_level(LORA_PWR_PIN, 0) // 控制loRa模块供电,低电平OFF

#define LoRa_1200 0x00   // 模块串口 波特率 1200
#define LoRa_2400 0x20   // 模块串口 波特率 2400
#define LoRa_4800 0x40   // 模块串口 波特率 4800
#define LoRa_9600 0x60   // 模块串口 波特率 9600
#define LoRa_19200 0x80  // 模块串口 波特率 19200
#define LoRa_38400 0xA0  // 模块串口 波特率 38400
#define LoRa_57600 0xC0  // 模块串口 波特率 57600
#define LoRa_115200 0xE0 // 模块串口 波特率 115200

#define LoRa_8N1 0x00 // 模块串口参数 8数据位 无校验 1停止位
#define LoRa_8O1 0x08 // 模块串口参数 8数据位 奇校验 1停止位
#define LoRa_8E1 0x10 // 模块串口参数 8数据位 偶校验 1停止位

#define LoRa_2_4 0x02  // 模块空中速率 2.4K
#define LoRa_4_8 0x03  // 模块空中速率 4.8K
#define LoRa_9_6 0x04  // 模块空中速率 9.6K
#define LoRa_19_2 0x05 // 模块空中速率 19.2K
#define LoRa_38_4 0x06 // 模块空中速率 38.4K
#define LoRa_62_5 0x07 // 模块空中速率 62.5K

#define LoRa_Data240 0x00 // 数据分包大小 240字节
#define LoRa_Data128 0x40 // 数据分包大小 128字节
#define LoRa_Data64 0x80  // 数据分包大小 64字节
#define LoRa_Data32 0xC0  // 数据分包大小 32字节

#define LoRa_RssiEN 0x20  // 启用RSSI功能
#define LoRa_RssiDIS 0x00 // 禁用RSSI功能

#define LoRa_SoftEN 0x10  // 启用软件切换模块工作模式功能
#define LoRa_SoftDIS 0x00 // 禁用软件切换模块工作模式功能

#define LoRa_FEC_22DBM 0x00 // 发射功率22dbm
#define LoRa_FEC_17DBM 0x01 // 发射功率17dbm
#define LoRa_FEC_13DBM 0x02 // 发射功率13dbm
#define LoRa_FEC_10DBM 0x03 // 发射功率10dbm

#define LoRa_CH0 0x00  // 模块信道 对应频率410MHz
#define LoRa_CH1 0x01  // 模块信道 对应频率411MHz
#define LoRa_CH2 0x02  // 模块信道 对应频率412MHz
#define LoRa_CH3 0x03  // 模块信道 对应频率413MHz
#define LoRa_CH4 0x04  // 模块信道 对应频率414MHz
#define LoRa_CH5 0x05  // 模块信道 对应频率415MHz
#define LoRa_CH6 0x06  // 模块信道 对应频率416MHz
#define LoRa_CH7 0x07  // 模块信道 对应频率417MHz
#define LoRa_CH8 0x08  // 模块信道 对应频率418MHz
#define LoRa_CH9 0x09  // 模块信道 对应频率419MHz
#define LoRa_CH10 0x0A // 模块信道 对应频率420MHz
#define LoRa_CH11 0x0B // 模块信道 对应频率421MHz
#define LoRa_CH12 0x0C // 模块信道 对应频率422MHz
#define LoRa_CH13 0x0D // 模块信道 对应频率423MHz
#define LoRa_CH14 0x0E // 模块信道 对应频率424MHz
#define LoRa_CH15 0x0F // 模块信道 对应频率425MHz
#define LoRa_CH16 0x10 // 模块信道 对应频率426MHz
#define LoRa_CH17 0x11 // 模块信道 对应频率427MHz
#define LoRa_CH18 0x12 // 模块信道 对应频率428MHz
#define LoRa_CH19 0x13 // 模块信道 对应频率429MHz
#define LoRa_CH20 0x14 // 模块信道 对应频率430MHz
#define LoRa_CH21 0x15 // 模块信道 对应频率431MHz
#define LoRa_CH22 0x16 // 模块信道 对应频率432MHz
#define LoRa_CH23 0x17 // 模块信道 对应频率433MHz
#define LoRa_CH24 0x18 // 模块信道 对应频率434MHz
#define LoRa_CH25 0x19 // 模块信道 对应频率435MHz
#define LoRa_CH26 0x1A // 模块信道 对应频率436MHz
#define LoRa_CH27 0x1B // 模块信道 对应频率437MHz
#define LoRa_CH28 0x1C // 模块信道 对应频率438MHz
#define LoRa_CH29 0x1D // 模块信道 对应频率439MHz
#define LoRa_CH30 0x1E // 模块信道 对应频率440MHz
#define LoRa_CH31 0x1F // 模块信道 对应频率441MHz
#define LoRa_CH32 0x20 // 模块信道 对应频率442MHz
#define LoRa_CH33 0x21 // 模块信道 对应频率442MHz
#define LoRa_CH34 0x22 // 模块信道 对应频率444MHz
#define LoRa_CH35 0x23 // 模块信道 对应频率445MHz
#define LoRa_CH36 0x24 // 模块信道 对应频率446MHz
#define LoRa_CH37 0x25 // 模块信道 对应频率447MHz
#define LoRa_CH38 0x26 // 模块信道 对应频率448MHz
#define LoRa_CH39 0x27 // 模块信道 对应频率449MHz
#define LoRa_CH40 0x28 // 模块信道 对应频率450MHz
#define LoRa_CH41 0x29 // 模块信道 对应频率451MHz
#define LoRa_CH42 0x2A // 模块信道 对应频率452MHz
#define LoRa_CH43 0x2B // 模块信道 对应频率453MHz
#define LoRa_CH44 0x2C // 模块信道 对应频率454MHz
#define LoRa_CH45 0x2D // 模块信道 对应频率455MHz
#define LoRa_CH46 0x2E // 模块信道 对应频率456MHz
#define LoRa_CH47 0x2F // 模块信道 对应频率457MHz
#define LoRa_CH48 0x30 // 模块信道 对应频率458MHz
#define LoRa_CH49 0x31 // 模块信道 对应频率459MHz
#define LoRa_CH50 0x32 // 模块信道 对应频率460MHz
#define LoRa_CH51 0x33 // 模块信道 对应频率461MHz
#define LoRa_CH52 0x34 // 模块信道 对应频率462MHz
#define LoRa_CH53 0x35 // 模块信道 对应频率463MHz
#define LoRa_CH54 0x36 // 模块信道 对应频率464MHz
#define LoRa_CH55 0x37 // 模块信道 对应频率465MHz
#define LoRa_CH56 0x38 // 模块信道 对应频率466MHz
#define LoRa_CH57 0x39 // 模块信道 对应频率467MHz
#define LoRa_CH58 0x3A // 模块信道 对应频率468MHz
#define LoRa_CH59 0x3B // 模块信道 对应频率469MHz
#define LoRa_CH60 0x3C // 模块信道 对应频率470MHz
#define LoRa_CH61 0x3D // 模块信道 对应频率471MHz
#define LoRa_CH62 0x3E // 模块信道 对应频率472MHz
#define LoRa_CH63 0x3F // 模块信道 对应频率473MHz
#define LoRa_CH64 0x40 // 模块信道 对应频率474MHz
#define LoRa_CH65 0x41 // 模块信道 对应频率475MHz
#define LoRa_CH66 0x42 // 模块信道 对应频率476MHz
#define LoRa_CH67 0x43 // 模块信道 对应频率477MHz
#define LoRa_CH68 0x44 // 模块信道 对应频率478MHz
#define LoRa_CH69 0x45 // 模块信道 对应频率479MHz
#define LoRa_CH70 0x46 // 模块信道 对应频率480MHz
#define LoRa_CH71 0x47 // 模块信道 对应频率481MHz
#define LoRa_CH72 0x48 // 模块信道 对应频率482MHz
#define LoRa_CH73 0x49 // 模块信道 对应频率483MHz
#define LoRa_CH74 0x4A // 模块信道 对应频率484MHz
#define LoRa_CH75 0x4B // 模块信道 对应频率485MHz
#define LoRa_CH76 0x4C // 模块信道 对应频率486MHz
#define LoRa_CH77 0x4D // 模块信道 对应频率487MHz
#define LoRa_CH78 0x4E // 模块信道 对应频率488MHz
#define LoRa_CH79 0x4F // 模块信道 对应频率489MHz
#define LoRa_CH80 0x50 // 模块信道 对应频率490MHz
#define LoRa_CH81 0x51 // 模块信道 对应频率491MHz
#define LoRa_CH82 0x52 // 模块信道 对应频率492MHz
#define LoRa_CH83 0x53 // 模块信道 对应频率493MHz

#define LoRa_RssiByteEN 0x80  // 启用RSSI字节功能
#define LoRa_RssiByteDIS 0x00 // 禁用RSSI字节功能

#define LoRa_ModeTRANS 0x00 // 模块透明传输
#define LoRa_ModePOINT 0x40 // 模块定点传输

#define LoRa_RelayEN 0x20  // 启用中继
#define LoRa_RelayDIS 0x00 // 禁用中继

#define LoRa_LBTEN 0x10  // 启用LBT
#define LoRa_LBTDIS 0x00 // 禁用LBT

#define LoRa_WorTX 0x08 // Wor模式发送
#define LoRa_WorRX 0x00 // Wor模式接收

#define LoRa_Wor500ms 0x00  // Wor周期500毫秒
#define LoRa_Wor1000ms 0x01 // Wor周期1000毫秒
#define LoRa_Wor1500ms 0x02 // Wor周期1500毫秒
#define LoRa_Wor2000ms 0x03 // Wor周期2000毫秒
#define LoRa_Wor2500ms 0x04 // Wor周期2500毫秒
#define LoRa_Wor3000ms 0x05 // Wor周期3000毫秒
#define LoRa_Wor3500ms 0x06 // Wor周期3500毫秒
#define LoRa_Wor4000ms 0x07 // Wor周期4000毫秒

#define ADDRES_REGISTER 0x0400   // 设备地址寄存器
#define VERSION1_REGISTER 0x0401 // 设备版本号1寄存器
#define VERSION2_REGISTER 0x0402 // 设备版本号2寄存器
#define VERSION3_REGISTER 0x0403 // 设备版本号3寄存器

#define TEMP_REGISTER 0x0300   // 温度数据寄存器
#define HUMI_REGISTER 0x0301   // 湿度数据寄存器
#define LIHGHT_REGISTER 0x0302 // 光强度数据寄存器
#define LED_REGISTER 0x0303    // LED数据寄存器
#define ADC1_REGISTER 0x0304   // ADC1数据寄存器
#define ADC2_REGISTER 0x0305   // ADC2数据寄存器
#define ADC3_REGISTER 0x0306   // ADC3数据寄存器

extern LoRaCB lora; // 外部变量声明，LoRa模块控制结构体

#define LoRa_autocheck 3000 // 自动检查间隔时间 单位：毫秒

// void LoRa_Init(void);                      // 函数声明，初始化LoRa模块
void LoRa_ConfigData(uint8_t *, uint16_t); // 函数声明，处理LoRa配置状态的数据
void LoRa_TransData(uint8_t *, uint16_t); // 函数声明，处理LoRa传输状态的数据
void LoRa_ActiveEvent(void);              // 函数声明，loRa主动事件
void LoRa_OTA(uint16_t); // 函数声明，LoRa子设备OTA传输数据
void LoRa_ProcessOTA(uint8_t *, uint16_t); // 函数声明，处理LoRa接收到的OTA数据

#define LLCC68_LORA_DEFAULT_STOP_TIMER_ON_PREAMBLE                             \
  LLCC68_BOOL_FALSE /**< disable stop timer on preamble */
#define LLCC68_LORA_DEFAULT_REGULATOR_MODE                                     \
  LLCC68_REGULATOR_MODE_DC_DC_LDO                     /**< only ldo */
#define LLCC68_LORA_DEFAULT_PA_CONFIG_DUTY_CYCLE 0x02 /**< set +17dBm power */
#define LLCC68_LORA_DEFAULT_PA_CONFIG_HP_MAX 0x03     /**< set +17dBm power */
#define LLCC68_LORA_DEFAULT_TX_DBM 17                 /**< +17dBm */
#define LLCC68_LORA_DEFAULT_RAMP_TIME                                          \
  LLCC68_RAMP_TIME_10US                         /**< set ramp time 10 us */
#define LLCC68_LORA_DEFAULT_SF LLCC68_LORA_SF_9 /**< sf9 */
#define LLCC68_LORA_DEFAULT_BANDWIDTH                                          \
  LLCC68_LORA_BANDWIDTH_125_KHZ                   /**< 125khz */
#define LLCC68_LORA_DEFAULT_CR LLCC68_LORA_CR_4_5 /**< cr4/5 */
#define LLCC68_LORA_DEFAULT_LOW_DATA_RATE_OPTIMIZE                             \
  LLCC68_BOOL_FALSE /**< disable low data rate optimize */
#define LLCC68_LORA_DEFAULT_RF_FREQUENCY 480000000U /**< 480000000Hz */
#define LLCC68_LORA_DEFAULT_SYMB_NUM_TIMEOUT 0      /**< 0 */
#define LLCC68_LORA_DEFAULT_SYNC_WORD 0x3444U       /**< public network */
#define LLCC68_LORA_DEFAULT_RX_GAIN 0x94            /**< common rx gain */
#define LLCC68_LORA_DEFAULT_OCP 0x38                /**< 140 mA */
#define LLCC68_LORA_DEFAULT_PREAMBLE_LENGTH 12      /**< 12 */
#define LLCC68_LORA_DEFAULT_HEADER                                             \
  LLCC68_LORA_HEADER_EXPLICIT               /**< explicit header */
#define LLCC68_LORA_DEFAULT_BUFFER_SIZE 255 /**< 255 */
#define LLCC68_LORA_DEFAULT_CRC_TYPE LLCC68_LORA_CRC_TYPE_ON /**< crc on */
#define LLCC68_LORA_DEFAULT_INVERT_IQ                                          \
  LLCC68_BOOL_FALSE /**< disable invert iq */
#define LLCC68_LORA_DEFAULT_CAD_SYMBOL_NUM                                     \
  LLCC68_LORA_CAD_SYMBOL_NUM_2              /**< 2 symbol */
#define LLCC68_LORA_DEFAULT_CAD_DET_PEAK 24 /**< 24 */
#define LLCC68_LORA_DEFAULT_CAD_DET_MIN 10  /**< 10 */
#define LLCC68_LORA_DEFAULT_START_MODE                                         \
  LLCC68_START_MODE_WARM /**< warm mode                                        \
                          */
#define LLCC68_LORA_DEFAULT_RTC_WAKE_UP                                        \
  LLCC68_BOOL_TRUE /**< enable rtc wake up */

uint8_t LoRa_Init(void);
uint8_t LoRa_EnterRxMode(void);
void LoRa_RcvData(void);
void LoRa_ConfigData(uint8_t *data, uint16_t data_len);
uint8_t LoRa_SendData(uint8_t *data, uint16_t len);

uint16_t CRC16_Modbus(uint8_t *pdata, uint16_t len);

#endif
