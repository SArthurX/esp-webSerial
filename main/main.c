#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

#define WIFI_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define UART_PORT_NUM UART_NUM_2
#define TX_PIN 4
#define RX_PIN 5
#define UART_BAUD_RATE 115200
#define BUF_SIZE 1024

static const char *TAG = "web_serial";
static httpd_handle_t server = NULL;
static int global_ws_fd = -1;

const char index_html[] = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>ESP32 Serial Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: 'Courier New', monospace; background: #1e1e1e; color: #00ff00; padding: 20px; margin: 0; }
    h2 { border-bottom: 1px solid #333; padding-bottom: 10px; }
    #terminal { 
      background: #000; 
      border: 1px solid #333; 
      height: 80vh; 
      overflow-y: auto; 
      padding: 10px; 
      white-space: pre-wrap; 
      word-wrap: break-word;
    }
    ::-webkit-scrollbar { width: 10px; }
    ::-webkit-scrollbar-track { background: #333; }
    ::-webkit-scrollbar-thumb { background: #888; border-radius: 5px; }
    ::-webkit-scrollbar-thumb:hover { background: #555; }
  </style>
</head>
<body>
  <h2>ESP-WebSerial</h2>
  <div id="status" style="color:gray; font-size:0.8em">Connecting...</div>
  <div id="terminal"></div>

<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  const MAX_LOG_LENGTH = 100000; 

  function initWebSocket() {
    websocket = new WebSocket(gateway);
    websocket.onopen = () => { 
        document.getElementById('status').innerText = 'Connected'; 
        document.getElementById('status').style.color = 'lime';
    };
    websocket.onclose = () => { 
        document.getElementById('status').innerText = 'Disconnected'; 
        document.getElementById('status').style.color = 'red';
        setTimeout(initWebSocket, 2000); 
    };
    websocket.onmessage = onMessage;
  }

  function onMessage(event) {
    var term = document.getElementById('terminal');
    var isAtBottom = (term.scrollHeight - term.scrollTop <= term.clientHeight + 30);
    term.insertAdjacentHTML('beforeend', event.data);

    if (term.innerText.length > MAX_LOG_LENGTH) {
        var currentText = term.innerText;
        term.innerText = currentText.substring(currentText.length - (MAX_LOG_LENGTH * 0.8));
        if(isAtBottom) term.scrollTop = term.scrollHeight;
    }

    if (isAtBottom) {
        term.scrollTop = term.scrollHeight;
    }
  }

  window.addEventListener('load', initWebSocket);
</script>
</body>
</html>
)rawliteral";

void init_uart() {
  uart_config_t uart_config = {
      .baud_rate = UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
  uart_param_config(UART_PORT_NUM, &uart_config);
  uart_set_pin(UART_PORT_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE,
               UART_PIN_NO_CHANGE);
}

static esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "Handshake done, the new connection is opened");
    global_ws_fd = httpd_req_to_sockfd(req);
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  uint8_t *buf = NULL;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK)
    return ret;

  if (ws_pkt.len) {
    buf = calloc(1, ws_pkt.len + 1);
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Received packet with message: %s", ws_pkt.payload);
    }
    free(buf);
  }
  return ret;
}

void send_ws_message(const char *msg) {
  if (server == NULL || global_ws_fd == -1)
    return;

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.payload = (uint8_t *)msg;
  ws_pkt.len = strlen(msg);
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  httpd_ws_send_frame_async(server, global_ws_fd, &ws_pkt);
}

static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static void start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;

  ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root_uri = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = root_get_handler,
                            .user_ctx = NULL};
    httpd_register_uri_handler(server, &root_uri);
    httpd_uri_t ws_uri = {.uri = "/ws",
                          .method = HTTP_GET,
                          .handler = ws_handler,
                          .user_ctx = NULL,
                          .is_websocket = true};
    httpd_register_uri_handler(server, &ws_uri);
  } else {
    ESP_LOGE(TAG, "Error starting server!");
  }
}

static void rx_task(void *arg) {
  uint8_t *data = (uint8_t *)malloc(BUF_SIZE + 1);
  while (1) {
    int rxBytes = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE,
                                  100 / portTICK_PERIOD_MS);
    if (rxBytes > 0) {
      data[rxBytes] = 0;
      send_ws_message((char *)data);
      // printf("Sent to WS: %s", data);
    }
  }
  free(data);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    ESP_LOGI(TAG, "Retry to connect to the AP");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    start_webserver();
  }
}

void wifi_init_sta(void) {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &wifi_event_handler, NULL,
                                      &instance_any_id);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &wifi_event_handler, NULL,
                                      &instance_got_ip);

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
          },
  };
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  esp_wifi_start();
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  init_uart();
  wifi_init_sta();
  xTaskCreate(rx_task, "uart_rx_task", 4096, NULL, configMAX_PRIORITIES - 1,
              NULL);
}
