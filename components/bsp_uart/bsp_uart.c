#include "bsp_uart.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_ppp.h"
#include "esp_netif_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lora.h"
#include "sdkconfig.h"
#include "wifi_cat1.h"
#include <assert.h>

static const char *TAG = "BSP_UART";
extern Sys_CB SysCB;
static esp_netif_t *cat1_netif = NULL;
static EventGroupHandle_t event_group = NULL;
const int CONNECT_BIT = BIT0;
// 串口事件队列句柄，用于在任务中阻塞等待串口数据
// static QueueHandle_t uart_lora_queue;

/**
 * @brief esp-modem 错误回调函数
 *
 * @param error 错误类型
 */
static void on_error_cb(esp_modem_terminal_error_t error) {
  if (error == ESP_MODEM_TERMINAL_BUFFER_OVERFLOW) {
    ESP_LOGW(TAG, "esp-modem 串口缓冲区溢出");
  } else if (error == ESP_MODEM_TERMINAL_DEVICE_GONE) {
    ESP_LOGE(TAG, "4G 模块设备已断开连接");
  } else if (error == ESP_MODEM_TERMINAL_UNKNOWN_ERROR) {
    ESP_LOGE(TAG, "esp-modem 发生未知错误");
  }
}

// PPP 网络状态事件回调：
// - 当 PPPoS 成功拨号并获得 IP 时，ESP-IDF 会通过 IP_EVENT_PPP_GOT_IP 通知
// - 当 PPP 链路断开、失去 IP 时，会通过 IP_EVENT_PPP_LOST_IP 通知
// 这里通常用事件组(EventGroup)把“拿到 IP 了”这个状态同步给拨号流程的主任务。
static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
  if (event_id == IP_EVENT_PPP_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "PPPoS GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(event_group, CONNECT_BIT);
    SysCB.SysEventFlag |= CONNECT_CAT1;
  } else if (event_id == IP_EVENT_PPP_LOST_IP) {
    ESP_LOGW(TAG, "PPPoS LOST IP");
    xEventGroupClearBits(event_group, CONNECT_BIT);
    SysCB.SysEventFlag &= ~CONNECT_CAT1;
  }
}
void Cat1_PPPoS_Init(void) {
  // 事件组：用于在不同任务/回调之间同步状态（这里用一个 bit 表示“已获得 IP”）
  event_group = xEventGroupCreate();

  // 初始化 ESP-NETIF：这是 ESP-IDF 网络栈适配层，WiFi/以太网/PPP 都基于它
  ESP_ERROR_CHECK(esp_netif_init());

  // 注册 IP 事件回调
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL);

  // 配置 DTE (Data Terminal Equipment)
  esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
  dte_config.uart_config.tx_io_num = CAT1_TX_PIN;
  dte_config.uart_config.rx_io_num = CAT1_RX_PIN;
  dte_config.uart_config.baud_rate = 115200;
  dte_config.uart_config.rx_buffer_size = 4096;
  dte_config.uart_config.tx_buffer_size = 512;
  dte_config.uart_config.port_num = UART_NUM_CAT1;

  // 配置 DCE (Data Circuit-terminating Equipment)
  esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CAT1_APN);

  // 创建 PPP 网络接口
  esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
  cat1_netif = esp_netif_new(&netif_ppp_config);
  assert(cat1_netif != NULL);

  // 实例化 DCE (EC800 通常可以使用 GENERIC 或 SIM7600/EC20 驱动)
  esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_GENERIC, &dte_config,
                                           &dce_config, cat1_netif);
  assert(dce != NULL);

  // 设置错误回调
  esp_modem_set_error_cb(dce, on_error_cb);
  ESP_LOGI(TAG, "正在等待4G初始化完成...");

  // 同步模块状态
  if (esp_modem_sync(dce) != ESP_OK) {
    ESP_LOGE(TAG, "4G模块初始化失败");
    return;
  } else {
    ESP_LOGI(TAG, "模块已就绪，正在进行 PPP 拨号...");
  }

  // 进入 DATA 模式
  if (esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA) != ESP_OK) {
    ESP_LOGE(TAG, "拨号失败无法进入数据模式");
    return;
  }

  // 阻塞等待拨号完成并获得 IP
  xEventGroupWaitBits(event_group, CONNECT_BIT, pdFALSE, pdFALSE,
                      portMAX_DELAY);
  ESP_LOGI(TAG, "4G模块拨号成功");
}

int bsp_uart_cat1_send(const char *data, int len) {
  return uart_write_bytes(UART_NUM_CAT1, data, len);
}
