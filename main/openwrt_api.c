#include "openwrt_api.h"
#include "app_config.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "OPENWRT_API";

// 💡 務必確認這是你路由器的真實 IP！
// 如果你的路由器允許 http (port 80)，強烈建議把 https 改成 http，速度快一倍且不耗記憶體！
#define OPENWRT_JSON_URL   "http://192.168.100.1/traffic.json" 
#define FETCH_INTERVAL_MS  1500

openwrt_data_t g_openwrt_data = {
    .download_speed = 0.0f,
    .upload_speed = 0.0f,
    .ping_ms = 0,
    .today_used = "0.00 MB",
    .month_used = "0.00 MB",
    .ip4 = "",
    .ip6 = "",
    .last_update_time = 0
};

// 靜態任務分佈於 PSRAM 的指針
static StackType_t *s_openwrt_task_stack = NULL;
static StaticTask_t s_openwrt_task_tcb;

static esp_err_t _openwrt_http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer = NULL;
    static int output_len = 0;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (output_buffer == NULL) { 
                output_buffer = malloc(evt->data_len + 1); 
                output_len = 0; 
            } else { 
                output_buffer = realloc(output_buffer, output_len + evt->data_len + 1); 
            }
            if (output_buffer == NULL) {
                ESP_LOGE(TAG, "記憶體不足，無法分配 JSON 緩衝區");
                return ESP_FAIL;
            }
            memcpy(output_buffer + output_len, evt->data, evt->data_len);
            output_len += evt->data_len; 
            output_buffer[output_len] = '\0';
            break;

        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL) {
                // ESP_LOGD(TAG, "收到 JSON: %s", output_buffer);

                cJSON *root = cJSON_Parse(output_buffer);
                if (root) {
                    cJSON *time_item = cJSON_GetObjectItem(root, "time");
                    if (time_item) g_openwrt_data.last_update_time = time_item->valueint;

                    cJSON *rx_item = cJSON_GetObjectItem(root, "rxbps");
                    cJSON *tx_item = cJSON_GetObjectItem(root, "txbps");
                    uint64_t current_rx = rx_item ? (uint64_t)rx_item->valuedouble : 0;
                    uint64_t current_tx = tx_item ? (uint64_t)tx_item->valuedouble : 0;

                    // 解析實時 Ping 值
                    cJSON *ping_item = cJSON_GetObjectItem(root, "ping");
                    if (ping_item) g_openwrt_data.ping_ms = ping_item->valueint;

                    // 解析今日與本月流量字串
                    cJSON *today_item = cJSON_GetObjectItem(root, "today");
                    cJSON *month_item = cJSON_GetObjectItem(root, "month");
                    if (today_item && today_item->valuestring) {
                        strncpy(g_openwrt_data.today_used, today_item->valuestring, sizeof(g_openwrt_data.today_used) - 1);
                    }
                    if (month_item && month_item->valuestring) {
                        strncpy(g_openwrt_data.month_used, month_item->valuestring, sizeof(g_openwrt_data.month_used) - 1);
                    }

                    g_openwrt_data.download_speed = (float)(current_rx) / 1024.0f / 1024.0f;
                    g_openwrt_data.upload_speed = (float)(current_tx) / 1024.0f / 1024.0f;

                    // 解析 IP 地址
                    cJSON *ip4_item = cJSON_GetObjectItem(root, "ip4");
                    cJSON *ip6_item = cJSON_GetObjectItem(root, "ip6");
                    if (ip4_item && ip4_item->valuestring) {
                        strncpy(g_openwrt_data.ip4, ip4_item->valuestring, sizeof(g_openwrt_data.ip4) - 1);
                    }
                    if (ip6_item && ip6_item->valuestring) {
                        strncpy(g_openwrt_data.ip6, ip6_item->valuestring, sizeof(g_openwrt_data.ip6) - 1);
                    }

                    cJSON_Delete(root);
                    ESP_LOGD(TAG, "✅ 更新成功 -> Ping: %dms | 下載: %.2f MB/s | 今日: %s", 
                             g_openwrt_data.ping_ms, g_openwrt_data.download_speed, g_openwrt_data.today_used);
                } else {
                    ESP_LOGE(TAG, "❌ JSON 解析失敗! 內容: %s", output_buffer);
                }
                free(output_buffer); 
                output_buffer = NULL; 
                output_len = 0;
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            if (output_buffer != NULL) { free(output_buffer); output_buffer = NULL; output_len = 0; }
            break;
        default: break;
    }
    return ESP_OK;
}

static void fetch_openwrt_task(void *pvParameters) {
    ESP_LOGI(TAG, "OpenWrt 監控任務正在 Core [%d] 上運行", xPortGetCoreID());

    while (1) {
        if (g_current_face != WATCH_FACE_OPENWRT) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        esp_http_client_config_t config = {
            .url = OPENWRT_JSON_URL,
            .timeout_ms = 2000,
            .event_handler = _openwrt_http_event_handler,
            
            // 忽略 HTTPS 自簽發憑證的嚴格驗證，防止底層握手報錯斷開！
            .cert_pem = NULL,
            .skip_cert_common_name_check = true,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client != NULL) {
            esp_http_client_set_header(client, "User-Agent", "ESP32-S3-GeekMonitor");
            esp_err_t err = esp_http_client_perform(client);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "⚠️ 請求失敗: %s (請檢查 IP 或路由器防火牆)", esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
        }

        vTaskDelay(pdMS_TO_TICKS(FETCH_INTERVAL_MS));
    }
}

void openwrt_api_init(void) {
    s_openwrt_task_stack = (StackType_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (s_openwrt_task_stack != NULL) {
        xTaskCreateStaticPinnedToCore(
            fetch_openwrt_task,
            "openwrt_fetch",
            8192 / sizeof(StackType_t),
            NULL,
            5,
            s_openwrt_task_stack,
            &s_openwrt_task_tcb,
            1 // 綁定至 Core 1
        );
        ESP_LOGI(TAG, "OpenWrt 任務已成功分配至 PSRAM 並綁定 Core 1");
    } else {
        ESP_LOGE(TAG, "PSRAM 分配失敗，降級使用內部 SRAM");
        xTaskCreatePinnedToCore(fetch_openwrt_task, "openwrt_fetch", 8192, NULL, 5, NULL, 1);
    }
}