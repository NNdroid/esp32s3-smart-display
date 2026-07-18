#include "system_utils.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip6_addr.h"

static const char *TAG = "SYS_UTILS";
static esp_netif_t *s_sta_netif = NULL;
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

char s_ip_address[32] = "未连接";
char s_ip6_address[64] = "未连接";
int s_wifi_rssi = -999;

esp_err_t nvs_save_u8(const char *key, uint8_t value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("stock_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_load_u8(const char *key, uint8_t *out_value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("stock_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    err = nvs_get_u8(handle, key, out_value);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_save_str(const char *key, const char *value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("stock_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_load_str(const char *key, char *out_value, size_t max_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("stock_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t len = max_len;
    err = nvs_get_str(handle, key, out_value, &len);
    nvs_close(handle);
    return err;
}

// 從 NVS 讀取上次保存的表盤 ID
void load_current_face_from_nvs(void) {
    nvs_handle_t handle;
    // 打開名為 "sys_cfg" 的命名空間 (只讀模式)
    if (nvs_open("sys_cfg", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t face_id = 0;
        if (nvs_get_u8(handle, "face_id", &face_id) == ESP_OK) {
            // ⚠️ 防呆校驗：假設你目前只有 3 個表盤 (0, 1, 2)
            // 如果從 NVS 讀出了大於 2 的非法數值（比如未初始化過的是 255），強制復位為 0
            if (face_id < 3) {
                g_current_face = (int)face_id;
                ESP_LOGI("NVS", "✅ 成功從 NVS 載入上次表盤 ID: [%d]", g_current_face);
            } else {
                g_current_face = 0;
                ESP_LOGW("NVS", "⚠️ 讀取的表盤 ID 非法，自動重置為默認表盤 [0]");
            }
        }
        nvs_close(handle);
    } else {
        ESP_LOGI("NVS", "ℹ️ 首次開機或未找到表盤配置，使用默認表盤 [0]");
    }
}

// 將當前表盤 ID 保存至 NVS
void save_current_face_to_nvs(void) {
    nvs_handle_t handle;
    // 打開 "sys_cfg" 命名空間 (讀寫模式)
    if (nvs_open("sys_cfg", NVS_READWRITE, &handle) == ESP_OK) {
        // 寫入數據
        nvs_set_u8(handle, "face_id", (uint8_t)g_current_face);
        // 💡 關鍵：必須呼叫 commit 才能真正寫入 Flash 晶片！
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI("NVS", "💾 當前表盤 ID [%d] 已成功儲存至 NVS！", g_current_face);
    } else {
        ESP_LOGE("NVS", "❌ 儲存表盤配置失敗！");
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "重試連接 Wi-Fi...");
        strcpy(s_ip_address, "未连接");
        strcpy(s_ip6_address, "未连接");
        s_wifi_rssi = -999;
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi 物理層連線成功！強制觸發 IPv6 引擎...");
        if (s_sta_netif != NULL) {
            esp_netif_create_ip6_linklocal(s_sta_netif);
            ESP_LOGI(TAG, "IPv6 引擎觸發指令已發送");
        } else {
            ESP_LOGW(TAG, "警告：STA 網卡指標為空，跳過 IPv6 啟動");
        }
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(s_ip_address, sizeof(s_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        refresh_wifi_status();
        ESP_LOGI(TAG, "Wi-Fi 連線成功！ IPv4: %s", s_ip_address);
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_GOT_IP6) {
        ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
        char temp_ipv6[64];
        snprintf(temp_ipv6, sizeof(temp_ipv6), IPV6STR, IPV62STR(event->ip6_info.ip));
        
        // 智能判斷：fe80 開頭是區域網 IP，其他的是公網 IP
        if (strncmp(temp_ipv6, "fe80", 4) == 0 || strncmp(temp_ipv6, "FE80", 4) == 0) {
            ESP_LOGI(TAG, "獲取到 IPv6 (區域 fe80): %s", temp_ipv6);
            if (strcmp(s_ip6_address, "未连接") == 0) {
                strcpy(s_ip6_address, temp_ipv6); // 先用區域 IP 墊檔
            }
        } else {
            ESP_LOGI(TAG, "獲取到 IPv6 (公網 Global): %s", temp_ipv6);
            strcpy(s_ip6_address, temp_ipv6); // 真正拿到公網 IP，覆寫保存
        }
    }
}

void wifi_init_basics(void) {
    s_wifi_event_group = xEventGroupCreate();
    
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif != NULL)
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char hostname[32];
        snprintf(hostname, sizeof(hostname), "ESP32-SX-%02X%02X%02X", mac[3], mac[4], mac[5]);
        esp_netif_set_hostname(s_sta_netif, hostname);
        ESP_LOGI(TAG, "Device Hostname set to: %s", hostname);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
}

void wait_for_wifi_connection(void) {
    ESP_LOGI(TAG, "等待 Wi-Fi 連線...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    refresh_wifi_status();
}

void refresh_wifi_status(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_wifi_rssi = ap_info.rssi;
    } else {
        s_wifi_rssi = -999;
    }
}

void sync_time_sntp(void) {
    ESP_LOGI(TAG, "正在向 NTP 伺服器同步時間...");
    
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.apple.com");
    // 關閉 DHCP 獲取，強制使用我們指定的公網 NTP，防止路由器搗亂
    sntp_config.server_from_dhcp = true; 
    
    // 加入阿里雲和騰訊雲 NTP，國內同步秒連
    sntp_config.servers[1] = "ntp.aliyun.com";
    sntp_config.servers[2] = "ntp.tencent.com"; 

    esp_netif_sntp_init(&sntp_config);
    
    int retry = 0;
    const int retry_count = 15;
    time_t now = 0;
    struct tm timeinfo = { 0 };

    // 回歸最暴力的年份判斷法，只要年份大於 2020，代表絕對同步成功了
    while (++retry < retry_count) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2020 - 1900)) {
            break; // 成功獲取時間，跳出迴圈
        }
        ESP_LOGI(TAG, "等待時間同步... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    if (timeinfo.tm_year >= (2020 - 1900)) {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "時間同步成功: %s", strftime_buf);
    } else {
        ESP_LOGW(TAG, "時間同步失敗！請檢查外網連線。");
    }
}