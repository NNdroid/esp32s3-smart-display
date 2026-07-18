#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdarg.h>

#define SYSLOG_SERVER_IP   "192.168.100.2"  // 💡 改成你 rsyslog 伺服器的內網 IP
#define SYSLOG_SERVER_PORT 514              // Syslog 標準 UDP 端口
#define SYSLOG_HOSTNAME    "ESP32-StockTV"  // 在 rsyslog 裡顯示的設備名

static int s_udp_sock = -1;
static struct sockaddr_in s_dest_addr;
static vprintf_like_t s_orig_vprintf = NULL; // 保存原本的串口輸出函數

// 💡 我們自訂的日誌攔截函數
static int syslog_vprintf_hook(const char *fmt, va_list args) {
    char log_buf[256];
    char syslog_buf[300];
    
    // 1. 先把格式化參數轉換成普通的文字字串
    int len = vsnprintf(log_buf, sizeof(log_buf), fmt, args);
    
    // 2. 如果 UDP Socket 已經建立（Wi-Fi 已連接），發送到遠端伺服器
    if (s_udp_sock >= 0 && len > 0) {
        // Syslog RFC 3164 簡化格式: <PRI>HOSTNAME TAG: MESSAGE
        // <14> 代表 Facility: User (1) * 8 + Severity: Info (6) = 14
        int syslog_len = snprintf(syslog_buf, sizeof(syslog_buf), "<14>%s %s", SYSLOG_HOSTNAME, log_buf);
        
        if (syslog_len > 0) {
            // 使用 UDP 發送（非阻塞，發完即走，極快！）
            sendto(s_udp_sock, syslog_buf, syslog_len, 0, (struct sockaddr *)&s_dest_addr, sizeof(s_dest_addr));
        }
    }

    // 3. 呼叫原本的串口輸出（如果你拔了線不想阻塞，也可以在這裡根據條件跳過）
    //if (s_orig_vprintf) {
    //    return s_orig_vprintf(fmt, args);
    //}
    return len;
}

// 💡 啟動 Syslog 重定向（請在 Wi-Fi 獲取到 IP 後調用！）
void start_syslog_redirect(void) {
    if (s_udp_sock >= 0) return; // 已啟動

    // 配置遠端 rsyslog 伺服器地址
    s_dest_addr.sin_addr.s_addr = inet_addr(SYSLOG_SERVER_IP);
    s_dest_addr.sin_family = AF_INET;
    s_dest_addr.sin_port = htons(SYSLOG_SERVER_PORT);

    // 建立 UDP Socket
    s_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_udp_sock < 0) {
        ESP_LOGE("SYSLOG", "無法建立 UDP Socket!");
        return;
    }

    // 設為非阻塞模式 (Non-blocking)，防止 Wi-Fi 訊號差時卡死觸控 Task！
    int flags = fcntl(s_udp_sock, F_GETFL, 0);
    fcntl(s_udp_sock, F_SETFL, flags | O_NONBLOCK);

    // 💡 劫持系統日誌！保存舊的，換成我們自己的 Hook 函數
    s_orig_vprintf = esp_log_set_vprintf(&syslog_vprintf_hook);
    
    ESP_LOGI("SYSLOG", "🌐 日誌重定向成功！所有日誌正飛向 %s:%d", SYSLOG_SERVER_IP, SYSLOG_SERVER_PORT);
}

// 💡 停止 Syslog 重定向，釋放 Socket 並歸還系統日誌控制權
void stop_syslog_redirect(void) {
    if (s_udp_sock < 0) return; // 本來就沒啟動

    // 1. 恢復 ESP-IDF 原本的串口日誌輸出函數
    if (s_orig_vprintf) {
        esp_log_set_vprintf(s_orig_vprintf);
        s_orig_vprintf = NULL;
    }

    // 2. 關閉並釋放 UDP Socket
    close(s_udp_sock);
    s_udp_sock = -1;

    ESP_LOGI("SYSLOG", "🛑 Syslog 重定向已停止，日誌輸出已完全歸還給本機串口！");
}