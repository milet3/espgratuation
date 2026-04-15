/**
 * Copyright (c) 2015 - present LibDriver All rights reserved
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * @file      driver_llcc68_interface_template.c
 * @brief     driver llcc68 interface template source file
 * @version   1.0.0
 * @author    Shifeng Li
 * @date      2023-04-15
 *
 * <h3>history</h3>
 * <table>
 * <tr><th>Date        <th>Version  <th>Author      <th>Description
 * <tr><td>2023/04/15  <td>1.0      <td>Shifeng Li  <td>first upload
 * </table>
 */

#include "driver_llcc68_interface.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include <string.h>

// 建议在 app_config.h 或这里定义 SPI 引脚

static spi_device_handle_t g_spi_handle; // SPI 设备句柄

/**
 * @brief  interface spi bus init
 * @return status code
 *         - 0 success
 *         - 1 spi init failed
 * @note   none
 */
uint8_t llcc68_interface_spi_init(void) {
  esp_err_t ret;

  // 1. 配置 SPI 总线参数
  spi_bus_config_t buscfg = {
      .miso_io_num = LORA_SPI_MISO,
      .mosi_io_num = LORA_SPI_MOSI,
      .sclk_io_num = LORA_SPI_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4096,
  };

  // 2. 配置 SPI 设备参数 (LLCC68 特性)
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz =
          8 * 1000 * 1000, // LLCC68 支持最高 16MHz，这里保守用 8MHz
      .mode = 0,           // SPI mode 0
      .spics_io_num = LORA_SPI_CS, // 片选引脚
      .queue_size = 7,             // 事务队列大小
  };

  // 3. 初始化 SPI 总线 (使用 SPI2_HOST，即原 HSPI)
  // 注意：如果总线已被其他设备初始化，这里会返回错误，可以根据实际情况调整
  ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE("LLCC68_INTF", "Failed to initialize SPI bus");
    return 1;
  }

  // 4. 将 LLCC68 设备挂载到总线上
  ret = spi_bus_add_device(SPI2_HOST, &devcfg, &g_spi_handle);
  if (ret != ESP_OK) {
    ESP_LOGE("LLCC68_INTF", "Failed to add SPI device");
    return 1;
  }

  return 0;
}

/**
 * @brief  interface spi bus deinit
 * @return status code
 *         - 0 success
 *         - 1 spi deinit failed
 * @note   none
 */
uint8_t llcc68_interface_spi_deinit(void) {

  return spi_bus_remove_device(g_spi_handle) == ESP_OK ? 0 : 1;
}

/**
 * @brief      interface spi bus write read
 * @param[in]  *in_buf points to a input buffer
 * @param[in]  in_len is the input length
 * @param[out] *out_buf points to a output buffer
 * @param[in]  out_len is the output length
 * @return     status code
 *             - 0 success
 *             - 1 write read failed
 * @note       none
 */
uint8_t llcc68_interface_spi_write_read(uint8_t *in_buf, uint32_t in_len,
                                        uint8_t *out_buf, uint32_t out_len) {
  spi_transaction_t t;
  esp_err_t ret;

  if (in_len == 0 && out_len == 0)
    return 0;

  memset(&t, 0, sizeof(t));

  // 如果 in_len 和 out_len 都不为 0，这通常是一个全双工或先写后读的操作
  // LLCC68 的驱动通常期望先发送指令，再接收返回
  // 如果驱动是分步调用的（先 call 只有 in_len 的，再 call 只有 out_len 的），
  // 那么我们需要确保 CS 在这两次调用之间保持低电平，或者通过一次 transaction
  // 完成。

  // 这里采用标准的 ESP-IDF SPI 事务处理方式：
  // 如果 in_len > 0, 则作为 tx_buffer 发送
  // 如果 out_len > 0, 则作为 rx_buffer 接收
  // 如果两者都 > 0, 则是同步读写（全双工）

  t.length = (in_len > out_len ? in_len : out_len) * 8;
  t.tx_buffer = in_len > 0 ? in_buf : NULL;
  t.rx_buffer = out_len > 0 ? out_buf : NULL;

  ret = spi_device_polling_transmit(g_spi_handle, &t);
  if (ret != ESP_OK) {
    return 1;
  }

  return 0;
}

/**
 * @brief  interface reset gpio init
 * @return status code
 *         - 0 success
 *         - 1 init failed
 * @note   none
 */
uint8_t llcc68_interface_reset_gpio_init(void) {
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << LORA_RESET_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  return gpio_config(&io_conf) == ESP_OK ? 0 : 1;
}

/**
 * @brief  interface reset gpio deinit
 * @return status code
 *         - 0 success
 *         - 1 deinit failed
 * @note   none
 */
uint8_t llcc68_interface_reset_gpio_deinit(void) {
  return gpio_reset_pin(LORA_RESET_PIN) == ESP_OK ? 0 : 1;
}

/**
 * @brief     interface reset gpio write
 * @param[in] data is the written data
 * @return    status code
 *            - 0 success
 *            - 1 write failed
 * @note      none
 */
uint8_t llcc68_interface_reset_gpio_write(uint8_t data) {
  if (data == 0)
    RESET_LOW();
  else
    RESET_HIGH();
  return 0;
}

/**
 * @brief  interface busy gpio init
 * @return status code
 *         - 0 success
 *         - 1 init failed
 * @note   none
 */
uint8_t llcc68_interface_busy_gpio_init(void) {
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << LORA_BUSY_PIN),
      .pull_down_en = 0,
      .pull_up_en = 1,
  };
  return gpio_config(&io_conf) == ESP_OK ? 0 : 1;
}

/**
 * @brief  interface busy gpio deinit
 * @return status code
 *         - 0 success
 *         - 1 deinit failed
 * @note   none
 */
uint8_t llcc68_interface_busy_gpio_deinit(void) {
  return gpio_reset_pin(LORA_BUSY_PIN) == ESP_OK ? 0 : 1;
}

/**
 * @brief      interface busy gpio read
 * @param[out] *value points to a value buffer
 * @return     status code
 *             - 0 success
 *             - 1 read failed
 * @note       none
 */
uint8_t llcc68_interface_busy_gpio_read(uint8_t *value) {
  *value = (uint8_t)BUSY_READ();
  return 0;
}

/**
 * @brief     interface delay ms
 * @param[in] ms
 * @note      none
 */
void llcc68_interface_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

/**
 * @brief     interface print format data
 * @param[in] fmt is the format data
 * @note      none
 */
#include <stdarg.h>
void llcc68_interface_debug_print(const char *const fmt, ...) {
  char str[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(str, sizeof(str), fmt, args);
  va_end(args);
  ESP_LOGI("LLCC68", "%s", str);
}

/**
 * @brief     interface receive callback
 * @param[in] type is the receive callback type
 * @param[in] *buf points to a buffer address
 * @param[in] len is the buffer length
 * @note      none
 */
void llcc68_interface_receive_callback(uint16_t type, uint8_t *buf,
                                       uint16_t len) {
  switch (type) {
  case LLCC68_IRQ_TX_DONE: {
    ESP_LOGI("LLCC68", "IRQ: Transmission completed.");
    break;
  }
  case LLCC68_IRQ_RX_DONE: {
    ESP_LOGI("LLCC68", "IRQ: Packet received. Length: %d", len);
    // 这里通常需要调用数据处理函数
    break;
  }
  case LLCC68_IRQ_PREAMBLE_DETECTED: {
    ESP_LOGD("LLCC68", "IRQ: Preamble detected.");
    break;
  }
  case LLCC68_IRQ_SYNC_WORD_VALID: {
    ESP_LOGD("LLCC68", "IRQ: Valid sync word detected.");
    break;
  }
  case LLCC68_IRQ_HEADER_VALID: {
    ESP_LOGD("LLCC68", "IRQ: Valid header detected.");
    break;
  }
  case LLCC68_IRQ_HEADER_ERR: {
    ESP_LOGE("LLCC68", "IRQ: Header error.");
    break;
  }
  case LLCC68_IRQ_CRC_ERR: {
    ESP_LOGE("LLCC68", "IRQ: CRC error.");
    break;
  }
  case LLCC68_IRQ_CAD_DONE: {
    ESP_LOGD("LLCC68", "IRQ: CAD done.");
    break;
  }
  case LLCC68_IRQ_CAD_DETECTED: {
    ESP_LOGI("LLCC68", "IRQ: CAD detected.");
    break;
  }
  case LLCC68_IRQ_TIMEOUT: {
    ESP_LOGW("LLCC68", "IRQ: Timeout.");
    break;
  }
  default: {
    ESP_LOGW("LLCC68", "IRQ: Unknown event (0x%04X).", type);
    break;
  }
  }
}
