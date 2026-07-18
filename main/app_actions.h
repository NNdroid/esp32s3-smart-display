#ifndef APP_ACTIONS_H
#define APP_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 核心動作一：切換表盤 (Switch Watch Face)
 * * 對應實體觸摸鍵的【長按 > 800ms】動作，以及 Web 端控制台的【切換下一表盤】按鈕。
 * 內部包含：
 * 1. 循環切換 g_current_face (0 -> 1 -> 2 -> 0)
 * 2. 自動保存新表盤 ID 至 NVS 記憶體
 * 3. 喚醒對應表盤的後台抓取線程 (如股票/OpenWrt)
 * 4. 蜂鳴器硬體音效回饋
 */
void app_action_switch_face(void);

/**
 * @brief 核心動作二：切換股票 (Switch Next Stock Ticker)
 * * 對應實體觸摸鍵的【短按 < 800ms】動作，以及 Web 端控制台的【切換下一股票】按鈕。
 * 內部包含：
 * 1. 僅在當前為股票表盤 (WATCH_FACE_STOCK) 時生效，否則忽略
 * 2. 循環切換 s_selected_stock 索引
 * 3. 儲存最新選中的股票代碼至 NVS (key: "last_ticker")
 * 4. 喚醒 HTTP 線程立即抓取新股票 K 線數據
 * 5. 蜂鳴器硬體音效回饋
 */
void app_action_switch_stock(void);

#ifdef __cplusplus
}
#endif

#endif // APP_ACTIONS_H