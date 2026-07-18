#include "ble_prov.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <stdio.h>

static char s_pin[7] = "000000";

const char* get_ble_pin(void) {
    return s_pin;
}

bool is_device_provisioned(void) {
    bool provisioned = false;
    network_prov_mgr_is_wifi_provisioned(&provisioned);
    return provisioned;
}

void start_new_network_provisioning(void) {
    // 生成 6 位隨機 PIN
    snprintf(s_pin, sizeof(s_pin), "%06lu", esp_random() % 1000000);

    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE
    };
    network_prov_mgr_init(config);
    
    network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, s_pin, "PROV_STOCK_S3", NULL);
    
    ESP_LOGI("BLE_PROV", "配網已啟動，PIN: %s", s_pin);
}

void reset_device_provisioning(void) {
    ESP_LOGW("BLE_PROV", "準備清除配網資訊與 Wi-Fi 憑證...");
    network_prov_mgr_reset_wifi_provisioning();
    esp_wifi_restore();
    ESP_LOGW("BLE_PROV", "清除完畢！");
}