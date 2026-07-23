#include "esp_log.h"
#include "app_config.h"
#include "app_actions.h"
#include "stock_api.h"
#include "openwrt_api.h"
#include "buzzer.h"
#include "system_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ACTION";

// 切換表盤
void app_action_switch_face(void) {
    ESP_LOGI(TAG, "🔄 執行動作：切換表盤");
    
    // 循環切換
    g_current_face = (g_current_face + 1) % WATCH_FACE_MAX; 
    
    // 儲存至 NVS (開機記憶)
    save_current_face_to_nvs();
    
    // 如果切到了股票表盤，立即踢醒後台線程抓數據
    if (g_current_face == WATCH_FACE_STOCK && s_fetch_task_handle != NULL) {
        xTaskNotify(s_fetch_task_handle, 1u, eSetBits);
    }
    
    // 喚醒主渲染循環 (為了 1 FPS 省電模式下的零延遲響應)
    if (g_main_task_handle != NULL) {
        xTaskNotifyGive(g_main_task_handle);
    }
    
    buzzer_beep(2700, 40);
}

// 切換股票
void app_action_switch_stock(void) {
    ESP_LOGI(TAG, "⚡ 執行動作：切換股票");
    if (g_current_face == WATCH_FACE_STOCK && s_stock_count > 0) {
        s_selected_stock = (s_selected_stock + 1) % s_stock_count;
        nvs_save_str("last_ticker", s_stock_list[s_selected_stock].ticker);
        
        // 立即踢醒後台線程抓取新股票 K 線
        if (s_fetch_task_handle != NULL) {
            xTaskNotify(s_fetch_task_handle, 1u, eSetBits);
        }
        
        // 喚醒主渲染循環
        if (g_main_task_handle != NULL) {
            xTaskNotifyGive(g_main_task_handle);
        }
        buzzer_beep(3200, 30);
    } else {
        ESP_LOGW(TAG, "當前不是股票表盤，忽略切換股票指令");
    }
}