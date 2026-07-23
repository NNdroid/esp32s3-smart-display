#include "mqtt_api.h"
#include "app_config.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"

static const char *TAG = "MQTT_API";

uint16_t *g_mqtt_image_buf = NULL;
bool g_mqtt_image_too_large = false;
bool g_mqtt_image_ready = false;
bool g_mqtt_connected = false;

static esp_mqtt_client_handle_t s_client = NULL;
static int s_expected_total = 0;
static int s_received_so_far = 0;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            g_mqtt_connected = true;
            if (strlen(g_mqtt_topic) > 0) {
                esp_mqtt_client_subscribe(s_client, g_mqtt_topic, 0);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            g_mqtt_connected = false;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            if (event->topic && event->topic_len > 0) {
                // New message started
                s_expected_total = event->total_data_len;
                s_received_so_far = 0;
                
                if (s_expected_total > 240 * 240 * 2) {
                    g_mqtt_image_too_large = true;
                    g_mqtt_image_ready = false;
                    ESP_LOGW(TAG, "Image too large! %d > 115200", s_expected_total);
                } else {
                    g_mqtt_image_too_large = false;
                    g_mqtt_image_ready = false;
                }
            }

            if (!g_mqtt_image_too_large && g_mqtt_image_buf != NULL) {
                if (s_received_so_far + event->data_len <= 240 * 240 * 2) {
                    memcpy((uint8_t*)g_mqtt_image_buf + s_received_so_far, event->data, event->data_len);
                    s_received_so_far += event->data_len;
                }
                
                if (s_received_so_far == s_expected_total && s_expected_total > 0) {
                    g_mqtt_image_ready = true;
                    ESP_LOGI(TAG, "Image receive complete! size=%d", s_received_so_far);
                    
                    // 喚醒主渲染循環 (為了 1 FPS 省電模式下的零延遲響應)
                    if (g_main_task_handle != NULL) {
                        xTaskNotifyGive(g_main_task_handle);
                    }
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

void mqtt_api_init(void) {
    if (strlen(g_mqtt_broker_url) == 0) {
        ESP_LOGI(TAG, "MQTT disabled (No URL)");
        return;
    }

    if (g_mqtt_image_buf == NULL) {
        g_mqtt_image_buf = (uint16_t*)heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM);
    }
    
    if (!g_mqtt_image_buf) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for MQTT Image");
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = g_mqtt_broker_url,
        .credentials.username = strlen(g_mqtt_username) > 0 ? g_mqtt_username : NULL,
        .credentials.authentication.password = strlen(g_mqtt_password) > 0 ? g_mqtt_password : NULL,
        .buffer.size = 8192,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .network.reconnect_timeout_ms = 5000, // 断线后每 5 秒自动重连一次
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client) {
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(s_client);
    }
}

void mqtt_api_reconnect(void) {
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    g_mqtt_connected = false;
    
    // 如果之前连接失败或断开，重置图片状态
    g_mqtt_image_too_large = false;
    g_mqtt_image_ready = false;
    
    mqtt_api_init();
}
