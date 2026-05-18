#include "bsp_uart.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
/*
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_ppp.h"
#include "esp_netif_types.h"
*/
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
// #include "lora.h"
// #include "lwip/inet.h"
#include "sdkconfig.h"
#include "wifi_cat1.h"
#include <assert.h>

static const char *TAG = "BSP_UART";
extern Sys_CB SysCB;
// static esp_netif_t *cat1_netif = NULL;
// static EventGroupHandle_t event_group = NULL;
const int CONNECT_BIT = BIT0;
// 串口事件队列句柄，用于在任务中阻塞等待串口数据
// static QueueHandle_t uart_lora_queue;

/**
 * @brief esp-modem 错误回调函数
 *
 * @param error 错误类型
 */
/*
static void on_error_cb(esp_modem_terminal_error_t error) {
  if (error == ESP_MODEM_TERMINAL_BUFFER_OVERFLOW) {
    ESP_LOGW(TAG, "esp-modem 串口缓冲区溢出");
  } else if (error == ESP_MODEM_TERMINAL_DEVICE_GONE) {
    ESP_LOGE(TAG, "4G 模块设备已断开连接");
  } else if (error == ESP_MODEM_TERMINAL_UNKNOWN_ERROR) {
    ESP_LOGE(TAG, "esp-modem 发生未知错误");
  }
}
*/

// PPP 网络状态事件回调：
// - 当 PPPoS 成功拨号并获得 IP 时，ESP-IDF 会通过 IP_EVENT_PPP_GOT_IP 通知
// - 当 PPP 链路断开、失去 IP 时，会通过 IP_EVENT_PPP_LOST_IP 通知
// 这里通常用事件组(EventGroup)把“拿到 IP 了”这个状态同步给拨号流程的主任务。
/*
static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
  if (event_id == IP_EVENT_PPP_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "PPPoS GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));

    // 1. 将 PPP 接口设置为默认网络接口
    esp_netif_set_default_netif(event->esp_netif);

    // 2. 手动设置 DNS，确保域名解析可靠
    esp_netif_dns_info_t dns;
    // 使用国内更稳定的 DNS 服务器
    dns.ip.u_addr.ip4.addr = ipaddr_addr("114.114.114.114"); // 114 DNS
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns);

    dns.ip.u_addr.ip4.addr = ipaddr_addr("223.5.5.5"); // 阿里 DNS
    esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_BACKUP, &dns);

    ESP_LOGI(TAG, "已手动设置 DNS: 114.114.114.114, 223.5.5.5");

    xEventGroupSetBits(event_group, CONNECT_BIT);
    SysCB.SysEventFlag |= CONNECT_CAT1;
  } else if (event_id == IP_EVENT_PPP_LOST_IP) {
    ESP_LOGW(TAG, "PPPoS LOST IP");
    xEventGroupClearBits(event_group, CONNECT_BIT);
    SysCB.SysEventFlag &= ~CONNECT_CAT1;
  }
}
*/

esp_err_t Cat1_AT_Init(void) {
  // 1. 初始化 4G 模块控制引脚
  WiFi_Cat1_InitGPIO();

  // 【安全检查】如果 CAT1 被分配到了 UART0 (调试模式)，则跳过初始化
  if (UART_NUM_CAT1 == UART_NUM_0) {
    ESP_LOGW(TAG, "检测到 CAT1 被分配至 UART0 (调试模式)，将跳过 4G "
                  "模块初始化以避免干扰控制台");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "正在初始化 Cat1 模块串口 (AT 模式)...");

  bool synced = false;

  // 2. 配置 UART
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t ret =
      uart_driver_install(UART_NUM_CAT1, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
  if (ret != ESP_OK) {
    return ret;
  }
  ret = uart_param_config(UART_NUM_CAT1, &uart_config);
  if (ret != ESP_OK) {
    return ret;
  }
  ret = uart_set_pin(UART_NUM_CAT1, CAT1_TX_PIN, CAT1_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (ret != ESP_OK) {
    return ret;
  }

  // 3. 硬件上电稳定逻辑 (移除硬件 IO 控制，仅保留延时等待模块启动)
  ESP_LOGI(TAG, "正在等待4G模块启动稳定 (5秒)...");
  vTaskDelay(pdMS_TO_TICKS(5000));

  // 4. 发送 AT 测试同步
  ESP_LOGI(TAG, "正在进行 AT 同步...");
  for (int i = 0; i < 5; i++) {
    uart_write_bytes(UART_NUM_CAT1, "AT\r\n", 4);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t data[128];
    int len = uart_read_bytes(UART_NUM_CAT1, data, sizeof(data) - 1,
                              pdMS_TO_TICKS(500));
    if (len > 0) {
      data[len] = '\0';
      ESP_LOGI(TAG, "Cat1 响应: %s", (char *)data);
      if (strstr((char *)data, "OK")) {
        ESP_LOGI(TAG, "Cat1 AT 同步成功");
        synced = true;
        break;
      }
    }
    ESP_LOGW(TAG, "AT 同步重试 (%d/5)...", i + 1);
  }

  if (!synced) {
    ESP_LOGE(TAG, "Cat1 AT 同步失败");
    return ESP_FAIL;
  }

  // 5. 检查 SIM 卡和网络状态
  ESP_LOGI(TAG, "正在检查 SIM 卡和网络状态...");
  const char *check_cmds[] = {
      "AT+CPIN?\r\n",  // 检查 SIM 卡
      "AT+CSQ\r\n",    // 检查信号强度
      "AT+CREG?\r\n",  // 检查网络注册
      "AT+CGATT?\r\n", // 检查 GPRS 附着
  };

  for (int i = 0; i < sizeof(check_cmds) / sizeof(char *); i++) {
    uart_write_bytes(UART_NUM_CAT1, check_cmds[i], strlen(check_cmds[i]));
    vTaskDelay(pdMS_TO_TICKS(1000));

    uint8_t data[128];
    int len = uart_read_bytes(UART_NUM_CAT1, data, sizeof(data) - 1,
                              pdMS_TO_TICKS(500));
    if (len > 0) {
      data[len] = '\0';
      ESP_LOGI(TAG, "CMD %s -> %s", check_cmds[i], (char *)data);
    }
  }

  SysCB.SysEventFlag |= CONNECT_CAT1; // 标记模块已就绪
  return ESP_OK;
}

/*
esp_err_t Cat1_PPPoS_Init(void) {
  // 1. 初始化 4G 模块引脚
  WiFi_Cat1_InitGPIO();

  ESP_LOGI(TAG, "正在等待4G模块上电稳定 (10秒)...");
  // 增加到 10 秒延迟，EC800 等模块启动较慢
  vTaskDelay(pdMS_TO_TICKS(10000));

  // 2. 事件组：用于在不同任务/回调之间同步状态
  event_group = xEventGroupCreate();

  // 初始化 ESP-NETIF
  esp_err_t ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    return ret;
  }

  // 注册 IP 事件回调
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL);

  // 配置 DTE
  esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
  dte_config.uart_config.tx_io_num = CAT1_TX_PIN;
  dte_config.uart_config.rx_io_num = CAT1_RX_PIN;
  dte_config.uart_config.baud_rate = 115200;
  dte_config.uart_config.rx_buffer_size = 4096;
  dte_config.uart_config.tx_buffer_size = 512;
  dte_config.uart_config.port_num = UART_NUM_CAT1;
  dte_config.dte_buffer_size = 1024; // 增加 DTE 缓冲区
  // 显式配置引脚，确保没有冲突
  dte_config.uart_config.flow_control = UART_HW_FLOWCTRL_DISABLE;
  dte_config.uart_config.source_clk = UART_SCLK_DEFAULT;

  // 配置 DCE
  esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CAT1_APN);

  // 创建 PPP 网络接口
  esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
  cat1_netif = esp_netif_new(&netif_ppp_config);
  assert(cat1_netif != NULL);

  // 实例化 DCE
  esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_GENERIC, &dte_config,
                                           &dce_config, cat1_netif);
  if (dce == NULL) {
    ESP_LOGE(TAG, "创建 esp_modem 设备失败");
    return ESP_FAIL;
  }

  // 设置错误回调
  esp_modem_set_error_cb(dce, on_error_cb);

  // 3. 增强同步逻辑
  bool synced = false;
  ESP_LOGI(TAG, "开始波特率自适应探测 (115200)...");

  for (int i = 0; i < 20; i++) {
    // 每次尝试前清空缓冲区，防止残留数据干扰
    uart_flush(UART_NUM_CAT1);

    // 盲发 AT 指令，尝试唤醒自适应波特率
    uart_write_bytes(UART_NUM_CAT1, "AT\r\n", 4);
    vTaskDelay(pdMS_TO_TICKS(500));

    if (esp_modem_sync(dce) == ESP_OK) {
      synced = true;
      break;
    }

    if (i == 3) {
      ESP_LOGW(TAG, "同步失败，尝试等待模块重启稳定...");
      vTaskDelay(pdMS_TO_TICKS(10000)); // 等待重启稳定
    }

    if (i == 7) {
      ESP_LOGW(TAG, "尝试发送 '+++' 退出数据模式...");
      uart_write_bytes(UART_NUM_CAT1, "+++", 3);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (i == 12) {
      ESP_LOGW(TAG, "同步困难，尝试切换波特率或发送强制唤醒序列...");
      // 发送一些空字节或不同的换行符
      uart_write_bytes(UART_NUM_CAT1, "\r\n\r\nAT\r\n", 8);
    }
    ESP_LOGW(TAG, "同步重试中 (%d/20)...", i + 1);
  }

  if (!synced) {
    ESP_LOGE(TAG, "4G模块初始化失败：模块未响应 AT 指令");
    // 尝试销毁已创建的对象以防内存泄漏
    // esp_modem_destroy(dce); // 如果有这个函数的话
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "模块已同步，正在关闭回显 (ATE0)...");
  esp_modem_at(dce, "ATE0", NULL, 1000);

  // 1. 强制设置为全功能模式并检查 SIM 卡
  ESP_LOGI(TAG, "正在设置全功能模式 (CFUN=1)...");
  // esp_modem_set_flow_control 需要三个参数：dce, dce_flow, dte_flow
  esp_modem_set_flow_control(dce, ESP_MODEM_FLOW_CONTROL_NONE,
                             ESP_MODEM_FLOW_CONTROL_NONE);
  esp_modem_at(dce, "AT+CFUN=1", NULL, 2000);

  ESP_LOGI(TAG, "正在检查 SIM 卡状态...");
  char response[64];
  for (int i = 0; i < 5; i++) {
    if (esp_modem_at(dce, "AT+CPIN?", response, 2000) == ESP_OK) {
      if (strstr(response, "READY")) {
        ESP_LOGI(TAG, "SIM 卡已就绪");
        break;
      }
    }
    ESP_LOGW(TAG, "等待 SIM 卡就绪... (%d/5)", i + 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  // 2. 检查信号强度和网络注册
  ESP_LOGI(TAG, "正在检查网络注册状态...");
  int rssi = 0, ber = 0;
  for (int i = 0; i < 15; i++) { // 增加重试次数到 15 次 (约 30 秒)
    esp_modem_get_signal_quality(dce, &rssi, &ber);
    ESP_LOGI(TAG, "信号强度: rssi=%d (0-31), ber=%d", rssi, ber);

    if (rssi == 99) {
      ESP_LOGW(TAG, "无信号，请检查天线连接");
    }

    // 检查注册状态
    char ops[32];
    int act = 0;
    if (esp_modem_get_operator_name(dce, ops, &act) == ESP_OK) {
      ESP_LOGI(TAG, "已注册网络，运营商: %s, 接入技术: %d", ops, act);
      break;
    }

    // 如果没有注册，尝试查询 CREG 状态
    esp_modem_at(dce, "AT+CREG?", response, 1000);
    ESP_LOGW(TAG, "注册状态 (CREG): %s", response);

    ESP_LOGW(TAG, "等待网络注册... (%d/15)", i + 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  ESP_LOGI(TAG, "正在进行 PPP 拨号...");
  vTaskDelay(pdMS_TO_TICKS(1000)); // 增加拨号前的稳定延时

  // 增加重试机制
  bool dialed = false;
  for (int retry = 0; retry < 3; retry++) {
    ESP_LOGI(TAG, "尝试进入数据模式 (%d/3)...", retry + 1);
    // 进入 DATA 模式
    if (esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA) == ESP_OK) {
      ESP_LOGI(TAG, "已发送拨号指令，等待 IP...");
      // 阻塞等待拨号完成并获得 IP
      EventBits_t bits = xEventGroupWaitBits(event_group, CONNECT_BIT, pdFALSE,
                                             pdFALSE, pdMS_TO_TICKS(30000));
      if (bits & CONNECT_BIT) {
        dialed = true;
        break;
      } else {
        ESP_LOGW(TAG, "拨号超时或连接丢失，尝试退回命令模式并重试...");
        esp_modem_set_mode(dce, ESP_MODEM_MODE_COMMAND);
        vTaskDelay(pdMS_TO_TICKS(5000)); // 重试前给模块喘息时间
      }
    } else {
      ESP_LOGE(TAG, "无法进入数据模式 (%d/3)", retry + 1);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }

  if (dialed) {
    ESP_LOGI(TAG, "4G模块拨号成功");
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "4G模块拨号最终失败 (未获得 IP)");
    return ESP_ERR_TIMEOUT;
  }
}
*/

int bsp_uart_cat1_send(const char *data, int len) {
  return uart_write_bytes(UART_NUM_CAT1, data, len);
}
