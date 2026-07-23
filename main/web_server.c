#include "web_server.h"
#include "app_config.h"
#include "system_utils.h"
#include "stock_api.h"
#include "graphics.h"
#include "env_sensor_drv.h"
#include "app_actions.h"
#include "battery.h"
#include "mqtt_api.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "psa/crypto.h"
#include "esp_random.h"
#include "ble_prov.h"
#include "esp_system.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");

static char s_admin_pwd_hash[65] = {0};
static char s_auth_token[33] = {0};
static httpd_handle_t s_http_server = NULL;

static void compute_hash(const char *input, char *output) {
    uint8_t digest[32]; size_t digest_len = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)input, strlen(input), digest, sizeof(digest), &digest_len) == PSA_SUCCESS) {
        for(int i = 0; i < 32; i++) sprintf(&output[i*2], "%02x", digest[i]);
    } else {
        strcpy(output, "");
    }
}

void auth_init(void) {
    if (nvs_load_str("admin_pwd", s_admin_pwd_hash, sizeof(s_admin_pwd_hash)) != ESP_OK) {
        strncpy(s_admin_pwd_hash, "8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918", sizeof(s_admin_pwd_hash));
        nvs_save_str("admin_pwd", s_admin_pwd_hash);
    }
    if (nvs_load_str("auth_token", s_auth_token, sizeof(s_auth_token)) != ESP_OK || strlen(s_auth_token) == 0) {
        // 如果 NVS 裡沒有 Token（第一次開機），才隨機生成一個，並永久保存！
        snprintf(s_auth_token, sizeof(s_auth_token), "%08lx%08lx%08lx%08lx", esp_random(), esp_random(), esp_random(), esp_random());
        nvs_save_str("auth_token", s_auth_token);
    }
}

static bool verify_token(httpd_req_t *req) {
    if (s_auth_token[0] == '\0') return false;
    char auth_hdr[64];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) == ESP_OK) {
        if (strncmp(auth_hdr, "Bearer ", 7) == 0 && strcmp(auth_hdr + 7, s_auth_token) == 0) return true;
    }
    return false;
}

static char* read_http_body(httpd_req_t *req) {
    if (req->content_len <= 0) return NULL;
    char *buf = calloc(1, req->content_len + 1);
    if (!buf) return NULL;
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) { free(buf); return NULL; }
        received += ret;
    }
    return buf;
}

static esp_err_t html_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

static esp_err_t css_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, (const char *)style_css_start, style_css_end - style_css_start);
}

static esp_err_t api_login_handler(httpd_req_t *req) {
    char *body = read_http_body(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");

    cJSON *user = cJSON_GetObjectItem(json, "username");
    cJSON *pass = cJSON_GetObjectItem(json, "password");

    if (cJSON_IsString(user) && cJSON_IsString(pass) && strcmp(user->valuestring, "admin") == 0) {
        char input_hash[65]; compute_hash(pass->valuestring, input_hash);
        if (strcmp(input_hash, s_admin_pwd_hash) == 0) {
            // snprintf(s_auth_token, sizeof(s_auth_token), "%08lx%08lx%08lx%08lx", esp_random(), esp_random(), esp_random(), esp_random());
            char resp[128]; snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"token\":\"%s\"}", s_auth_token);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
            cJSON_Delete(json); return ESP_OK;
        }
    }
    cJSON_Delete(json);
    httpd_resp_set_status(req, "401 Unauthorized");
    return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"密碼錯誤\"}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_password_handler(httpd_req_t *req) {
    if (!verify_token(req)) { httpd_resp_set_status(req, "401 Unauthorized"); return httpd_resp_send(req, "", 0); }
    char *body = read_http_body(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
    
    cJSON *json = cJSON_Parse(body); free(body);
    if (!json) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");

    cJSON *old_pass = cJSON_GetObjectItem(json, "old_password");
    cJSON *new_pass = cJSON_GetObjectItem(json, "new_password");
    
    if (cJSON_IsString(old_pass) && cJSON_IsString(new_pass)) {
        char old_hash[65]; compute_hash(old_pass->valuestring, old_hash);
        if (strcmp(old_hash, s_admin_pwd_hash) == 0) {
            compute_hash(new_pass->valuestring, s_admin_pwd_hash);
            nvs_save_str("admin_pwd", s_admin_pwd_hash);
            // 密碼修改成功後，重新生成一個隨機 Token 並覆蓋 NVS！
            snprintf(s_auth_token, sizeof(s_auth_token), "%08lx%08lx%08lx%08lx", esp_random(), esp_random(), esp_random(), esp_random());
            nvs_save_str("auth_token", s_auth_token);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
            cJSON_Delete(json); return ESP_OK;
        }
    }
    cJSON_Delete(json);
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "{\"status\":\"error\",\"msg\":\"舊密碼錯誤\"}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_status_handler(httpd_req_t *req) {
    if (!verify_token(req)) { httpd_resp_set_status(req, "401 Unauthorized"); return httpd_resp_send(req, "", 0); }
    refresh_wifi_status();
    // 呼叫 bme280 讀取
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ip", s_ip_address);
    cJSON_AddStringToObject(root, "ip6", s_ip6_address);
    cJSON_AddStringToObject(root, "rssi", (s_wifi_rssi < -90) ? "弱" : (s_wifi_rssi < -70) ? "中" : "強");
    cJSON_AddNumberToObject(root, "temp", g_bme280_data.temperature);
    cJSON_AddNumberToObject(root, "hum", g_bme280_data.humidity_pct);
    cJSON_AddNumberToObject(root, "press", g_bme280_data.pressure_hpa);
    cJSON_AddBoolToObject(root, "has_sensor", g_bme280_data.sensor_type != SENSOR_TYPE_NONE);
    cJSON_AddNumberToObject(root, "battery", battery_get_percentage());
    cJSON_AddNumberToObject(root, "bat_mv", battery_get_voltage_mv());
    cJSON_AddBoolToObject(root, "mqtt_connected", g_mqtt_connected);
    
    char *resp = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    free(resp); cJSON_Delete(root); return ESP_OK;
}

static esp_err_t api_stocks_get_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *stock_array = cJSON_CreateArray();
    for(int i = 0; i < s_stock_count; i++) { 
        cJSON_AddItemToArray(stock_array, cJSON_CreateString(s_stock_list[i].ticker)); 
    }
    cJSON_AddItemToObject(root, "stocks", stock_array);
    
    cJSON_AddNumberToObject(root, "chartMode", (uint8_t)g_chart_mode);
    
    char *resp = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    
    free(resp); 
    cJSON_Delete(root); 
    return ESP_OK;
}

static esp_err_t api_stocks_post_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }
    char *body = read_http_body(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
    
    cJSON *cjson = cJSON_Parse(body); 
    free(body);
    if (!cjson) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");

    cJSON *arr = cJSON_GetObjectItem(cjson, "stocks");
    cJSON *chart_mode_item = cJSON_GetObjectItem(cjson, "chartMode");

    if (cJSON_IsArray(arr)) {
        int count = cJSON_GetArraySize(arr);
        s_stock_count = count > MAX_STOCKS ? MAX_STOCKS : count;
        
        for (int i = 0; i < s_stock_count; i++) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            if (cJSON_IsString(item) && item->valuestring != NULL) {
                strncpy(s_stock_list[i].ticker, item->valuestring, sizeof(s_stock_list[i].ticker) - 1);
                s_stock_list[i].ticker[sizeof(s_stock_list[i].ticker) - 1] = '\0';
                
                s_stock_list[i].display_name[0] = '\0';
                s_stock_list[i].market[0] = '\0';
            }
        }
        
        uint8_t mode_val = 0;
        if (chart_mode_item && cJSON_IsNumber(chart_mode_item)) {
            mode_val = (uint8_t)chart_mode_item->valueint;
        } else if (chart_mode_item && cJSON_IsString(chart_mode_item)) {
            mode_val = (atoi(chart_mode_item->valuestring) == 1) ? 1 : 0;
        }
        g_chart_mode = (chart_mode_t)mode_val; // 確保全域變數跟隨前端的選擇同步更新！

        save_stock_config_to_nvs();
        s_selected_stock = 0;
        nvs_save_str("last_ticker", s_stock_list[0].ticker);
        nvs_save_u8("chart_mode", mode_val);
        
        // 喚醒網絡任務抓取最新配置下的股票與圖表
        if (s_fetch_task_handle != NULL) {
            xTaskNotifyGive((TaskHandle_t)s_fetch_task_handle);
        }
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "{\"status\":\"error\"}", HTTPD_RESP_USE_STRLEN);
    }
    
    cJSON_Delete(cjson); 
    return ESP_OK;
}

static void delayed_restart_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    reset_device_provisioning();
    esp_restart();
    vTaskDelete(NULL);
}

static esp_err_t api_reset_wifi_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"rebooting\"}", HTTPD_RESP_USE_STRLEN);
    
    xTaskCreate(delayed_restart_task, "restart_task", 2048, NULL, 5, NULL);
    
    return ESP_OK;
}

static esp_err_t api_brightness_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }

    char buf[64];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) return ESP_FAIL;
    buf[received] = '\0';

    // 使用 cJSON 解析 (假設你專案已包含 cJSON)
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *b = cJSON_GetObjectItem(root, "brightness");
        if (b && cJSON_IsNumber(b)) {
            int brightness = b->valueint;
            // 呼叫我們封裝在 graphics 模組的亮度函數
            set_lcd_brightness(brightness);
            // 寫入 NVS 保存
            char val_str[8];
            snprintf(val_str, sizeof(val_str), "%d", brightness);
            nvs_save_str("lcd_bright", val_str);
        }
        cJSON_Delete(root);
    }
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_settings_battery_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }
    
    char *body = read_http_body(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");

    cJSON *json = cJSON_Parse(body);
    free(body);
    
    if (json) {
        cJSON *style = cJSON_GetObjectItem(json, "bat_style");
        cJSON *pos = cJSON_GetObjectItem(json, "bat_pos");
        
        if (cJSON_IsNumber(style)) {
            g_bat_style = style->valueint;
            nvs_save_u8("bat_style", g_bat_style);
        }
        if (cJSON_IsNumber(pos)) {
            g_bat_pos = pos->valueint;
            nvs_save_u8("bat_pos", g_bat_pos);
        }
        cJSON_Delete(json);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_settings_openwrt_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }
    
    char *body = read_http_body(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");

    cJSON *json = cJSON_Parse(body);
    free(body);
    
    if (json) {
        cJSON *url = cJSON_GetObjectItem(json, "url");
        
        if (cJSON_IsString(url)) {
            const char *new_url = url->valuestring;
            if (strlen(new_url) == 0) {
                // If empty, use default
                strncpy(g_openwrt_url, "http://192.168.100.1/traffic.json", sizeof(g_openwrt_url) - 1);
                nvs_save_str("openwrt_url", "");
            } else {
                strncpy(g_openwrt_url, new_url, sizeof(g_openwrt_url) - 1);
                nvs_save_str("openwrt_url", g_openwrt_url);
            }
        }
        cJSON_Delete(json);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_settings_mqtt_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }
    char *body = read_http_body(req);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (json) {
        cJSON *url = cJSON_GetObjectItem(json, "url");
        cJSON *topic = cJSON_GetObjectItem(json, "topic");
        cJSON *user = cJSON_GetObjectItem(json, "user");
        cJSON *pwd = cJSON_GetObjectItem(json, "pwd");

        if (cJSON_IsString(url)) {
            strncpy(g_mqtt_broker_url, url->valuestring, sizeof(g_mqtt_broker_url) - 1);
            nvs_save_str("mqtt_url", g_mqtt_broker_url);
        }
        if (cJSON_IsString(topic)) {
            strncpy(g_mqtt_topic, topic->valuestring, sizeof(g_mqtt_topic) - 1);
            nvs_save_str("mqtt_topic", g_mqtt_topic);
        }
        if (cJSON_IsString(user)) {
            strncpy(g_mqtt_username, user->valuestring, sizeof(g_mqtt_username) - 1);
            nvs_save_str("mqtt_user", g_mqtt_username);
        }
        if (cJSON_IsString(pwd)) {
            strncpy(g_mqtt_password, pwd->valuestring, sizeof(g_mqtt_password) - 1);
            nvs_save_str("mqtt_pwd", g_mqtt_password);
        }
        cJSON_Delete(json);
        
        // 动态重连 MQTT 客户端应用新配置
        mqtt_api_reconnect();
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_get_settings_handler(httpd_req_t *req) {
    if (!verify_token(req)) { 
        httpd_resp_set_status(req, "401 Unauthorized"); 
        return httpd_resp_send(req, "", 0); 
    }

    // 從 NVS 讀取亮度
    char bright_str[8] = "100"; // 預設 100
    nvs_load_str("lcd_bright", bright_str, sizeof(bright_str));

    // 回傳 JSON
    char response[1024];
    snprintf(response, sizeof(response), "{\"brightness\":%s, \"bat_style\":%d, \"bat_pos\":%d, \"openwrt_url\":\"%s\", \"mqtt_url\":\"%s\", \"mqtt_topic\":\"%s\", \"mqtt_user\":\"%s\"}", 
        bright_str, g_bat_style, g_bat_pos, g_openwrt_url, g_mqtt_broker_url, g_mqtt_topic, g_mqtt_username);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_ota_handler(httpd_req_t *req) {
    if (!verify_token(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_send(req, "", 0);
    }

    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);

    char *buf = malloc(1024);
    int remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buf, min_int(remaining, 1024));
        esp_ota_write(update_handle, buf, received);
        remaining -= received;
    }
    free(buf);

    esp_ota_end(update_handle);
    esp_ota_set_boot_partition(update_partition);
    
    httpd_resp_send(req, "{\"status\":\"ok\",\"msg\":\"Updating...\"}", HTTPD_RESP_USE_STRLEN);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static void reboot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    vTaskDelete(NULL);
}

static esp_err_t api_restart_handler(httpd_req_t *req) {
    if (!verify_token(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_send(req, "", 0);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\", \"action\":\"restart\"}");
    
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    
    return ESP_OK;
}

static esp_err_t api_switch_face_handler(httpd_req_t *req) {
    app_action_switch_face();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\", \"action\":\"switch_face\"}");
    return ESP_OK;
}

static esp_err_t api_switch_stock_handler(httpd_req_t *req) {
    app_action_switch_stock();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\", \"action\":\"switch_stock\"}");
    return ESP_OK;
}

void start_webserver(void) {
    if (s_http_server != NULL) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    
    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/", .method=HTTP_GET, .handler=html_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/style.css", .method=HTTP_GET, .handler=css_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/login", .method=HTTP_POST, .handler=api_login_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/password", .method=HTTP_POST, .handler=api_password_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/status", .method=HTTP_GET, .handler=api_status_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/stocks", .method=HTTP_GET, .handler=api_stocks_get_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/stocks", .method=HTTP_POST, .handler=api_stocks_post_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/reset_wifi", .method=HTTP_POST, .handler=api_reset_wifi_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/brightness", .method=HTTP_POST, .handler=api_brightness_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/settings", .method=HTTP_GET, .handler=api_get_settings_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/settings/battery", .method=HTTP_POST, .handler=api_settings_battery_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/settings/openwrt", .method=HTTP_POST, .handler=api_settings_openwrt_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/settings/mqtt", .method=HTTP_POST, .handler=api_settings_mqtt_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/ota", .method=HTTP_POST, .handler=api_ota_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/action/switch_face", .method=HTTP_GET, .handler=api_switch_face_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/action/switch_stock", .method=HTTP_GET, .handler=api_switch_stock_handler});
        httpd_register_uri_handler(s_http_server, &(httpd_uri_t){.uri="/api/action/restart", .method=HTTP_GET, .handler=api_restart_handler});
    }
}