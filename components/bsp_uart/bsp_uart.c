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
#include "sdkconfig.h"
#include "wifi_cat1.h"
#include <assert.h>
#include "lwip/inet.h"

static const char *TAG = "BSP_UART";
extern Sys_CB SysCB;

static esp_netif_t *cat1_netif = NULL;
static EventGroupHandle_t event_group = NULL;
static esp_modem_dce_t *s_dce = NULL;

const int CONNECT_BIT = BIT0;

static void on_error_cb(esp_modem_terminal_error_t error)
{
  if (error == ESP_MODEM_TERMINAL_BUFFER_OVERFLOW)
  {
    ESP_LOGW(TAG, "esp-modem UART buffer overflow");
  }
  else if (error == ESP_MODEM_TERMINAL_DEVICE_GONE)
  {
    ESP_LOGE(TAG, "4G module device disconnected");
  }
  else if (error == ESP_MODEM_TERMINAL_UNKNOWN_ERROR)
  {
    ESP_LOGE(TAG, "esp-modem unknown error");
  }
}

static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
  if (event_id == IP_EVENT_PPP_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "PPPoS GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
    esp_netif_set_default_netif(event->esp_netif);

    esp_netif_dns_info_t dns;
    dns.ip.u_addr.ip4.addr = ipaddr_addr("114.114.114.114");
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns);
    dns.ip.u_addr.ip4.addr = ipaddr_addr("223.5.5.5");
    esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_BACKUP, &dns);
    ESP_LOGI(TAG, "DNS set: 114.114.114.114 / 223.5.5.5");

    xEventGroupSetBits(event_group, CONNECT_BIT);
    SysCB.SysEventFlag |= CONNECT_CAT1;
  }
  else if (event_id == IP_EVENT_PPP_LOST_IP)
  {
    ESP_LOGW(TAG, "PPPoS LOST IP");
    xEventGroupClearBits(event_group, CONNECT_BIT);
    SysCB.SysEventFlag &= ~CONNECT_CAT1;
  }
}

esp_err_t Cat1_AT_Init(void)
{
  WiFi_Cat1_InitGPIO();
  if (UART_NUM_CAT1 == UART_NUM_0)
  {
    ESP_LOGW(TAG, "CAT1 on UART0 (debug), skipping init");
    return ESP_OK;
  }
  ESP_LOGI(TAG, "Init Cat1 UART (AT mode)...");
  bool synced = false;
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  esp_err_t ret = uart_driver_install(UART_NUM_CAT1, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
  if (ret != ESP_OK)
    return ret;
  ret = uart_param_config(UART_NUM_CAT1, &uart_config);
  if (ret != ESP_OK)
    return ret;
  ret = uart_set_pin(UART_NUM_CAT1, CAT1_TX_PIN, CAT1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (ret != ESP_OK)
    return ret;
  ESP_LOGI(TAG, "Waiting for 4G module startup (5s)...");
  vTaskDelay(pdMS_TO_TICKS(5000));
  ESP_LOGI(TAG, "AT sync...");
  for (int i = 0; i < 5; i++)
  {
    uart_write_bytes(UART_NUM_CAT1, "AT\r\n", 4);
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t data[128];
    int len = uart_read_bytes(UART_NUM_CAT1, data, sizeof(data) - 1, pdMS_TO_TICKS(500));
    if (len > 0)
    {
      data[len] = '\0';
      if (strstr((char *)data, "OK"))
      {
        ESP_LOGI(TAG, "Cat1 AT sync OK");
        synced = true;
        break;
      }
    }
    ESP_LOGW(TAG, "AT sync retry (%d/5)...", i + 1);
  }
  if (!synced)
  {
    ESP_LOGE(TAG, "Cat1 AT sync failed");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Checking SIM and network...");
  const char *check_cmds[] = {"AT+CPIN?\r\n", "AT+CSQ\r\n", "AT+CREG?\r\n", "AT+CGATT?\r\n"};
  for (int i = 0; i < sizeof(check_cmds) / sizeof(char *); i++)
  {
    uart_write_bytes(UART_NUM_CAT1, check_cmds[i], strlen(check_cmds[i]));
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint8_t data[128];
    int len = uart_read_bytes(UART_NUM_CAT1, data, sizeof(data) - 1, pdMS_TO_TICKS(500));
    if (len > 0)
    {
      data[len] = '\0';
      ESP_LOGI(TAG, "CMD %s -> %s", check_cmds[i], (char *)data);
    }
  }
  SysCB.SysEventFlag |= CONNECT_CAT1;
  return ESP_OK;
}

esp_err_t Cat1_PPPoS_Init(void)
{
  WiFi_Cat1_InitGPIO();
  if (UART_NUM_CAT1 == UART_NUM_0)
  {
    ESP_LOGW(TAG, "CAT1 on UART0 (debug), PPPOS not available");
    return ESP_ERR_NOT_SUPPORTED;
  }
  ESP_LOGI(TAG, "Waiting for 4G module startup (10s)...");
  CAT1_POWER(0);
  vTaskDelay(pdMS_TO_TICKS(10000));

  event_group = xEventGroupCreate();
  esp_err_t ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    return ret;
  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    return ret;
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL);

  esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
  dte_config.uart_config.tx_io_num = CAT1_TX_PIN;
  dte_config.uart_config.rx_io_num = CAT1_RX_PIN;
  dte_config.uart_config.baud_rate = 115200;
  dte_config.uart_config.rx_buffer_size = 4096;
  dte_config.uart_config.tx_buffer_size = 512;

  esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CAT1_APN);

  esp_netif_config_t ppp_netif_config = ESP_NETIF_DEFAULT_PPP();
  cat1_netif = esp_netif_new(&ppp_netif_config);
  assert(cat1_netif != NULL);

  s_dce = esp_modem_new_dev(ESP_MODEM_DCE_GENERIC, &dte_config, &dce_config, cat1_netif);
  if (s_dce == NULL)
  {
    ESP_LOGE(TAG, "Failed to create esp_modem device");
    return ESP_FAIL;
  }
  esp_modem_set_error_cb(s_dce, on_error_cb);

  bool synced = false;
  ESP_LOGI(TAG, "Probing AT sync at 115200...");
  for (int i = 0; i < 20; i++)
  {
    uart_flush(UART_NUM_CAT1);
    uart_write_bytes(UART_NUM_CAT1, "AT\r\n", 4);
    vTaskDelay(pdMS_TO_TICKS(500));
    if (esp_modem_sync(s_dce) == ESP_OK)
    {
      synced = true;
      break;
    }
    if (i == 3)
    {
      ESP_LOGW(TAG, "Sync slow, waiting reboot...");
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
    if (i == 7)
    {
      ESP_LOGW(TAG, "Sending +++ exit data mode...");
      uart_write_bytes(UART_NUM_CAT1, "+++", 3);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (i == 12)
    {
      ESP_LOGW(TAG, "Sending wake-up sequence...");
      uart_write_bytes(UART_NUM_CAT1, "\r\n\r\nAT\r\n", 8);
    }
    ESP_LOGW(TAG, "Sync retry (%d/20)...", i + 1);
  }
  if (!synced)
  {
    ESP_LOGE(TAG, "4G module not responding");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Disabling echo (ATE0)...");
  esp_modem_at(s_dce, "ATE0", NULL, 1000);
  ESP_LOGI(TAG, "Setting CFUN=1...");
  esp_modem_set_flow_control(s_dce, ESP_MODEM_FLOW_CONTROL_NONE, ESP_MODEM_FLOW_CONTROL_NONE);
  esp_modem_at(s_dce, "AT+CFUN=1", NULL, 2000);

  ESP_LOGI(TAG, "Checking SIM card...");
  char response[64];
  for (int i = 0; i < 5; i++)
  {
    if (esp_modem_at(s_dce, "AT+CPIN?", response, 2000) == ESP_OK)
    {
      if (strstr(response, "READY"))
      {
        ESP_LOGI(TAG, "SIM card ready");
        break;
      }
    }
    ESP_LOGW(TAG, "Waiting SIM ready... (%d/5)", i + 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  ESP_LOGI(TAG, "Checking network registration...");
  int rssi = 0, ber = 0;
  for (int i = 0; i < 15; i++)
  {
    esp_modem_get_signal_quality(s_dce, &rssi, &ber);
    ESP_LOGI(TAG, "Signal: rssi=%d ber=%d", rssi, ber);
    if (rssi == 99)
      ESP_LOGW(TAG, "No signal, check antenna");
    char ops[32];
    int act = 0;
    if (esp_modem_get_operator_name(s_dce, ops, &act) == ESP_OK)
    {
      ESP_LOGI(TAG, "Registered: operator=%s act=%d", ops, act);
      break;
    }
    esp_modem_at(s_dce, "AT+CREG?", response, 1000);
    ESP_LOGW(TAG, "CREG: %s", response);
    ESP_LOGW(TAG, "Waiting registration... (%d/15)", i + 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  ESP_LOGI(TAG, "Starting PPP dial-up...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  bool dialed = false;
  for (int retry = 0; retry < 3; retry++)
  {
    ESP_LOGI(TAG, "Entering data mode (%d/3)...", retry + 1);
    if (esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA) == ESP_OK)
    {
      ESP_LOGI(TAG, "Dial sent, waiting for IP...");
      EventBits_t bits = xEventGroupWaitBits(event_group, CONNECT_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
      if (bits & CONNECT_BIT)
      {
        dialed = true;
        break;
      }
      else
      {
        ESP_LOGW(TAG, "Dial timeout, retrying...");
        esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
        vTaskDelay(pdMS_TO_TICKS(5000));
      }
    }
    else
    {
      ESP_LOGE(TAG, "Cannot enter data mode (%d/3)", retry + 1);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
  if (dialed)
  {
    ESP_LOGI(TAG, "4G PPPoS dial-up succeeded");
    return ESP_OK;
  }
  else
  {
    ESP_LOGE(TAG, "4G PPPoS dial-up failed (no IP)");
    return ESP_ERR_TIMEOUT;
  }
}

bool cat1_pppos_is_connected(void) { return (SysCB.SysEventFlag & CONNECT_CAT1) != 0; }
esp_netif_t *cat1_pppos_get_netif(void) { return cat1_netif; }
int bsp_uart_cat1_send(const char *data, int len) { return uart_write_bytes(UART_NUM_CAT1, data, len); }
