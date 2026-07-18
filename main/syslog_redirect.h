#ifndef SYSLOG_REDIRECT_H
#define SYSLOG_REDIRECT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 啟動 Syslog 日誌重定向
 * 
 * 💡 使用說明：
 * 必須在 Wi-Fi 成功連接並獲取到 IP 地址 (IP_EVENT_STA_GOT_IP) 之後調用！
 * 啟動後，系統所有的 ESP_LOGI / W / E 及 printf 日誌將透過 UDP (Port 514)
 * 即時飛向你設定的 rsyslog 伺服器。
 */
void start_syslog_redirect(void);

/**
 * @brief 停止 Syslog 日誌重定向（可選拓展）
 * 
 * 在 Wi-Fi 斷開連接，或者進入深度睡眠 (Deep Sleep) 前調用，
 * 用於釋放 UDP Socket 資源並恢復原始串口日誌管線。
 */
void stop_syslog_redirect(void);

#ifdef __cplusplus
}
#endif

#endif // SYSLOG_REDIRECT_H