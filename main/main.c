#include "app_config.h"
#include "system_utils.h"
#include "env_sensor_drv.h"
#include "stock_api.h"
#include "openwrt_api.h"
#include "web_server.h"
#include "graphics.h"
#include "syslog_redirect.h"
#include "nvs_flash.h"
#include "ble_prov.h"
#include "buzzer.h"
#include "app_actions.h"
#include "psa/crypto.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/touch_sens.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7789.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_check.h"

watch_face_t g_current_face = WATCH_FACE_STOCK;

// ==========================================
// 💡 靈敏度：既然是數值下降，建議設回 15~20 左右測試
// ==========================================
#define TOUCH_SENSITIVITY_PERCENT 15

// ==========================================
// 📈 表盤 1：股票走勢圖表盤 (完美整合分時折線與 K線，保障數據完整性)
// ==========================================
void draw_stock_face(uint16_t *fb)
{
    const stock_config_t *stock = &s_stock_list[s_selected_stock];

    // ==========================================
    // 💡 公共數據準備：計算最新價、漲跌額與漲跌百分比
    // ==========================================
    float latest_price = 0.0f;
    float prev_close = current_open_price; // 昨收價 (API 解析出的 chartPreviousClose)
    bool has_data = false;

    if (g_chart_mode == CHART_MODE_LINE && data_count > 0)
    {
        latest_price = stock_prices[data_count - 1];
        has_data = true;
    }
    else if (g_chart_mode == CHART_MODE_KLINE && kline_data_count > 0)
    {
        latest_price = kline_data[kline_data_count - 1].close;
        has_data = true;
    }

    if (!has_data)
    {
        draw_string(fb, 50, 110, "Syncing Data...", COLOR_WHITE, COLOR_BLACK, 1);
        return;
    }

    // 計算當日漲跌額與百分比
    float change = latest_price - prev_close;
    float percent = (prev_close > 0.0001f) ? ((change / prev_close) * 100.0f) : 0.0f;

    // 歐美配色標準：最新價高於昨收則為綠色 (漲)，低於昨收則為紅色 (跌)
    bool is_up = (latest_price >= prev_close);
    uint16_t theme_color = is_up ? COLOR_GREEN : COLOR_RED;
    uint16_t fill_color = is_up ? COLOR_SHADOW_GREEN : COLOR_SHADOW_RED;
    bool is_more_than_1 = (latest_price > 1.0f);

    // --- 模式 A：繪製 1 分鐘分時折線圖 ---
    if (g_chart_mode == CHART_MODE_LINE)
    {
        if (data_count > 1)
        {
            float min_p = stock_prices[0];
            float max_p = stock_prices[0];
            uint32_t max_vol = stock_volumes[0];

            for (int i = 1; i < data_count; i++)
            {
                if (stock_prices[i] < min_p)
                    min_p = stock_prices[i];
                if (stock_prices[i] > max_p)
                    max_p = stock_prices[i];
                if (stock_volumes[i] > max_vol)
                    max_vol = stock_volumes[i];
            }
            if (max_p - min_p < 0.0001f)
                max_p = min_p + 1.0f;
            if (max_vol == 0)
                max_vol = 1;

            // 1. 繪製背景網格與 Y 軸標籤
            for (int x = 40; x < LCD_H_RES; x += 40)
                draw_line(fb, x, 0, x, LCD_H_RES, COLOR_GRID);
            for (int y = 40; y <= 200; y += 40)
            {
                draw_line(fb, 0, y, LCD_H_RES, y, COLOR_GRID);
                float price_at_y = max_p - ((float)(y - 40) / 160.0f) * (max_p - min_p);
                char axis_str[8];
                snprintf(axis_str, sizeof(axis_str), is_more_than_1 ? "%.2f" : "%.4f", price_at_y);
                draw_string(fb, 0, y - 8, axis_str, COLOR_GRAY, COLOR_BLACK, 1);
            }

            // 昨收基準線 (虛線效果)
            int open_y = 230 - (int)((prev_close - min_p) / (max_p - min_p) * 220.0f);
            if (open_y >= 0 && open_y < LCD_V_RES)
            {
                for (int x = 0; x < LCD_H_RES; x += 6)
                    draw_line(fb, x, open_y, x + 3, open_y, COLOR_OPEN_LINE);
            }

            // 2. 繪製半透明網點陰影
            for (int i = 0; i < data_count - 1; i++)
            {
                int y0 = 230 - (int)((stock_prices[i] - min_p) / (max_p - min_p) * 220.0f);
                for (int fy = y0; fy < LCD_V_RES; fy++)
                {
                    if (fy >= 0 && fy < LCD_V_RES && ((fy + i) % 2 == 0))
                        fb[fy * LCD_H_RES + i] = fill_color;
                }
            }

            // 3. 繪製底部成交量柱狀圖
            for (int i = 0; i < data_count; i++)
            {
                int vol_h = (int)((float)stock_volumes[i] / (float)max_vol * 65.0f);
                if (vol_h > 0)
                {
                    int vol_y_start = 230 - vol_h;
                    bool bar_up = (i == 0) ? (stock_prices[0] >= prev_close) : (stock_prices[i] >= stock_prices[i - 1]);
                    uint16_t vol_color = bar_up ? COLOR_VOL_GREEN : COLOR_VOL_RED;
                    for (int vy = vol_y_start; vy <= 230; vy++)
                    {
                        if (vy >= 0 && vy < LCD_V_RES)
                            fb[vy * LCD_H_RES + i] = vol_color;
                    }
                }
            }

            // 4. 繪製主價格線
            for (int i = 0; i < data_count - 1; i++)
            {
                int y0 = 230 - (int)((stock_prices[i] - min_p) / (max_p - min_p) * 220.0f);
                int y1 = 230 - (int)((stock_prices[i + 1] - min_p) / (max_p - min_p) * 220.0f);
                draw_line(fb, i, y0, i + 1, y1, theme_color);
                draw_line(fb, i, y0 - 1, i + 1, y1 - 1, theme_color);
                draw_line(fb, i, y0 + 1, i + 1, y1 + 1, theme_color);
            }
            int last_y = 230 - (int)((latest_price - min_p) / (max_p - min_p) * 220.0f);
            draw_circle(fb, data_count - 1, last_y, 4, COLOR_WHITE);
            draw_circle(fb, data_count - 1, last_y, 2, theme_color);

            // 5. 繪製極值標記
            bool is_more_than_1 = (latest_price > 1.0f);
            char min_str[16], max_str[16];
            snprintf(max_str, sizeof(max_str), is_more_than_1 ? "H %.2f" : "H %.4f", max_p);
            snprintf(min_str, sizeof(min_str), is_more_than_1 ? "L %.2f" : "L %.4f", min_p);
            draw_string(fb, 6, 206, max_str, COLOR_WHITE, COLOR_BLACK, 1);
            draw_string(fb, 6, 220, min_str, COLOR_WHITE, COLOR_BLACK, 1);

            // 模式標誌
            draw_string(fb, 80, 220, "[LINE]", COLOR_YELLOW, COLOR_BLACK, 1);
        }
    }
    // --- 模式 B：繪製 15 分鐘 K 線圖 (Candlestick) ---
    else
    {
        if (kline_data_count > 0)
        {
            float min_p = kline_data[0].low;
            float max_p = kline_data[0].high;
            for (int i = 1; i < kline_data_count; i++)
            {
                if (kline_data[i].low < min_p)
                    min_p = kline_data[i].low;
                if (kline_data[i].high > max_p)
                    max_p = kline_data[i].high;
            }
            if (max_p - min_p < 0.0001f)
                max_p = min_p + 1.0f;

            // 1. 繪製網格
            for (int x = 40; x < LCD_H_RES; x += 40)
                draw_line(fb, x, 0, x, LCD_H_RES, COLOR_GRID);
            for (int y = 40; y <= 200; y += 40)
            {
                draw_line(fb, 0, y, LCD_H_RES, y, COLOR_GRID);
                float price_at_y = max_p - ((float)(y - 40) / 160.0f) * (max_p - min_p);
                char axis_str[8];
                snprintf(axis_str, sizeof(axis_str), is_more_than_1 ? "%.2f" : "%.4f", price_at_y);
                draw_string(fb, 0, y - 8, axis_str, COLOR_GRAY, COLOR_BLACK, 1);
            }

            int chart_top = 40;
            int chart_height = 160;
            int chart_left = 40;
            int step_x = 4;
            int candle_w = 3;

            // 2. 循環繪製每根蠟燭圖與換日日期線
            char last_drawn_date[6] = "";
            for (int i = 0; i < kline_data_count; i++)
            {
                kline_data_t *k = &kline_data[i];
                if (chart_left + (i + 1) * step_x > LCD_H_RES)
                    break;

                int center_x = chart_left + i * step_x + (step_x / 2);
                int box_x = center_x - (candle_w / 2);

                // 換日分割線與刻度
                bool is_new_day = (i == 0) || (strcmp(k->date, last_drawn_date) != 0);
                if (is_new_day)
                {
                    strncpy(last_drawn_date, k->date, sizeof(last_drawn_date) - 1);
                    for (int dy = 40; dy <= 200; dy += 4)
                    {
                        draw_line(fb, center_x, dy, center_x, dy + 1, COLOR_GRAY);
                    }
                    draw_line(fb, center_x, 40, center_x - 2, 42, COLOR_YELLOW);
                    draw_line(fb, center_x, 40, center_x + 2, 42, COLOR_YELLOW);
                    draw_line(fb, center_x, 200, center_x, 204, COLOR_YELLOW);
                    draw_string(fb, center_x - 12, 208, k->date, COLOR_YELLOW, COLOR_BLACK, 1);
                }
                else if (i % 10 == 0)
                {
                    draw_line(fb, center_x, 200, center_x, 203, COLOR_GRAY);
                    draw_string(fb, center_x - 12, 208, k->time, COLOR_GRAY, COLOR_BLACK, 1);
                }

                int y_high = chart_top + (int)((max_p - k->high) / (max_p - min_p) * chart_height);
                int y_low = chart_top + (int)((max_p - k->low) / (max_p - min_p) * chart_height);
                int y_open = chart_top + (int)((max_p - k->open) / (max_p - min_p) * chart_height);
                int y_close = chart_top + (int)((max_p - k->close) / (max_p - min_p) * chart_height);

                uint16_t color;
                int box_top, box_bottom;
                if (k->close >= k->open)
                {
                    color = COLOR_GREEN;
                    box_top = y_close;
                    box_bottom = y_open;
                }
                else
                {
                    color = COLOR_RED;
                    box_top = y_open;
                    box_bottom = y_close;
                }

                int box_h = box_bottom - box_top;
                if (box_h < 1)
                    box_h = 1;

                draw_line(fb, center_x, y_high, center_x, y_low, color);
                for (int rx = box_x; rx < box_x + candle_w; rx++)
                {
                    for (int ry = box_top; ry <= box_top + box_h; ry++)
                    {
                        if (rx >= 0 && rx < LCD_H_RES && ry >= 0 && ry < LCD_V_RES)
                        {
                            fb[ry * LCD_H_RES + rx] = color;
                        }
                    }
                }
            }

            // 3. 繪製極值標記
            char min_str[16], max_str[16];
            snprintf(max_str, sizeof(max_str), is_more_than_1 ? "H %.2f" : "H %.4f", max_p);
            snprintf(min_str, sizeof(min_str), is_more_than_1 ? "L %.2f" : "L %.4f", min_p);
            draw_string(fb, 6, 206, max_str, COLOR_WHITE, COLOR_BLACK, 1);
            draw_string(fb, 6, 220, min_str, COLOR_WHITE, COLOR_BLACK, 1);

            // 模式標誌
            draw_string(fb, 80, 220, "[15M K-LINE]", COLOR_CYAN, COLOR_BLACK, 1);
        }
    }

    // ==========================================
    // 💡 頂層公共 UI 繪製：統一股票標識與實時漲跌幅數據！
    // ==========================================
    // 1. 繪製左上角股票代碼與名稱
    draw_string(fb, 6, 6, stock->ticker, COLOR_WHITE, COLOR_BLACK, 2);
    draw_string(fb, 6, 30, stock->display_name, COLOR_WHITE, COLOR_BLACK, 1);

    // 2. 繪製右上角最新價格 (隨漲跌變色)
    char price_str[16];
    snprintf(price_str, sizeof(price_str), is_more_than_1 ? "%.2f" : "%.4f", latest_price);
    int price_start_x = LCD_V_RES - (strlen(price_str) * 12);
    draw_string(fb, price_start_x, 6, price_str, theme_color, COLOR_BLACK, 2);

    // 3. 繪製右上角今日漲跌幅與百分比 (如 +1.24(+1.15%)，隨漲跌變色)
    char chg_str[32];
    snprintf(chg_str, sizeof(chg_str), is_more_than_1 ? "%s%.2f(%.2f%%)" : "%s%.4f(%.2f%%)", change >= 0 ? "+" : "", change, percent);
    int chg_start_x = LCD_V_RES - (strlen(chg_str) * 6);
    draw_string(fb, chg_start_x, 26, chg_str, theme_color, COLOR_BLACK, 1);
}

// ==========================================
// 🌐 表盘 2：漂亮的 OpenWrt 流量表盘
// ==========================================
void draw_openwrt_face(uint16_t *fb)
{
    char str_buf[32];

    // ==========================================
    // 1. 頂部 HUD 抬頭
    // ==========================================
    draw_string(fb, 10, 6, "[ WAN ROUTING HUD ]", COLOR_CYAN, COLOR_BLACK, 1);

    if (g_openwrt_data.ping_ms > 0)
    {
        snprintf(str_buf, sizeof(str_buf), "%3dms", g_openwrt_data.ping_ms);
        uint16_t ping_color = (g_openwrt_data.ping_ms < 50) ? COLOR_GREEN : COLOR_YELLOW;
        draw_string(fb, 160, 6, "PING:", COLOR_GRAY, COLOR_BLACK, 1);
        draw_string(fb, 195, 6, str_buf, ping_color, COLOR_BLACK, 1);
    }
    else
    {
        draw_string(fb, 165, 6, "PING:ERR", COLOR_RED, COLOR_BLACK, 1);
    }

    draw_line(fb, 10, 17, 230, 17, COLOR_CYAN);
    draw_line(fb, 10, 19, 230, 19, COLOR_GRAY);

    // ==========================================
    // 2. 下載監控區
    // ==========================================
    draw_string(fb, 12, 26, "▼ DOWN STREAM", COLOR_GREEN, COLOR_BLACK, 1);

    float down_spd = g_openwrt_data.download_speed;
    if (down_spd >= 1.0f)
    {
        snprintf(str_buf, sizeof(str_buf), "%5.1f", down_spd);
        draw_string(fb, 12, 38, str_buf, COLOR_WHITE, COLOR_BLACK, 3);
        draw_string(fb, 125, 52, "MB/s", COLOR_GREEN, COLOR_BLACK, 1);
    }
    else
    {
        snprintf(str_buf, sizeof(str_buf), "%5.1f", down_spd * 1024.0f);
        draw_string(fb, 12, 38, str_buf, COLOR_WHITE, COLOR_BLACK, 3);
        draw_string(fb, 125, 52, "KB/s", COLOR_GREEN, COLOR_BLACK, 1);
    }

    for (int y = 66; y <= 69; y++)
    {
        draw_line(fb, 12, y, 228, y, 0x18E3);
    }
    int down_bar_width = (int)(216.0f * (down_spd / 100.0f));
    if (down_bar_width > 216)
        down_bar_width = 216;
    if (down_bar_width < 0)
        down_bar_width = 0;

    for (int x = 12; x < 12 + down_bar_width; x += 4)
    {
        for (int y = 66; y <= 69; y++)
        {
            draw_line(fb, x, y, (x + 2 > 12 + down_bar_width) ? (12 + down_bar_width) : (x + 2), y, COLOR_GREEN);
        }
    }

    // ==========================================
    // 3. 上傳監控區
    // ==========================================
    draw_string(fb, 12, 78, "▲ UP STREAM", COLOR_RED, COLOR_BLACK, 1);

    float up_spd = g_openwrt_data.upload_speed;
    if (up_spd >= 1.0f)
    {
        snprintf(str_buf, sizeof(str_buf), "%5.1f", up_spd);
        draw_string(fb, 12, 90, str_buf, COLOR_WHITE, COLOR_BLACK, 3);
        draw_string(fb, 125, 104, "MB/s", COLOR_RED, COLOR_BLACK, 1);
    }
    else
    {
        snprintf(str_buf, sizeof(str_buf), "%5.1f", up_spd * 1024.0f);
        draw_string(fb, 12, 90, str_buf, COLOR_WHITE, COLOR_BLACK, 3);
        draw_string(fb, 125, 104, "KB/s", COLOR_RED, COLOR_BLACK, 1);
    }

    for (int y = 118; y <= 121; y++)
    {
        draw_line(fb, 12, y, 228, y, 0x18E3);
    }
    int up_bar_width = (int)(216.0f * (up_spd / 30.0f));
    if (up_bar_width > 216)
        up_bar_width = 216;
    if (up_bar_width < 0)
        up_bar_width = 0;

    for (int x = 12; x < 12 + up_bar_width; x += 4)
    {
        for (int y = 118; y <= 121; y++)
        {
            draw_line(fb, x, y, (x + 2 > 12 + up_bar_width) ? (12 + up_bar_width) : (x + 2), y, COLOR_RED);
        }
    }

    // ==========================================
    // 4. 底部數據卡片區 (優化版佈局)
    // ==========================================
    // 分割線略微下移，與上方進度條保持舒適呼吸感
    draw_line(fb, 10, 130, 230, 130, COLOR_GRAY);

    draw_string(fb, 12, 142, "M:", COLOR_GRAY, COLOR_BLACK, 1);
    draw_string(fb, 28, 142, g_openwrt_data.month_used, COLOR_YELLOW, COLOR_BLACK, 1);

    draw_string(fb, 98, 142, "T:", COLOR_GRAY, COLOR_BLACK, 1);
    draw_string(fb, 114, 142, g_openwrt_data.today_used, COLOR_WHITE, COLOR_BLACK, 1);

    // 右側 LED 狀態指示燈與行對齊
    if (g_openwrt_data.ping_ms > 0 && g_openwrt_data.ping_ms < 50)
    {
        draw_string(fb, 192, 142, " ON ", COLOR_BLACK, COLOR_GREEN, 1);
    }
    else if (g_openwrt_data.ping_ms >= 50)
    {
        draw_string(fb, 192, 142, " DLY ", COLOR_BLACK, COLOR_YELLOW, 1);
    }
    else
    {
        draw_string(fb, 192, 142, " ERR ", COLOR_WHITE, COLOR_RED, 1);
    }

    draw_string(fb, 12, 168, "IP4:", COLOR_GRAY, COLOR_BLACK, 1);
    draw_string(fb, 42, 168, g_openwrt_data.ip4, COLOR_WHITE, COLOR_BLACK, 1);

    draw_string(fb, 12, 188, "IP6:", COLOR_GRAY, COLOR_BLACK, 1);
    // draw_string(fb, 42, 188, g_openwrt_data.ip6, COLOR_WHITE, COLOR_BLACK, 1);
    draw_scrolling_string(fb, 42, 188, g_openwrt_data.ip6, COLOR_YELLOW, COLOR_BLACK, 1, 215);

    // ==========================================
    // 5. 底部天气数据行
    // ==========================================
    if (g_bme280_data.sensor_type != SENSOR_TYPE_NONE)
    {
        typedef struct
        {
            char name[2];
            char unit[8];
            float value;
        } data_item_t;

        data_item_t data_items[3];
        int item_count = 0;

        // 如果温度大于0，则添加温度数据项
        if (g_bme280_data.temperature > 0)
        {
            data_items[item_count++] = (data_item_t){"T", "%.2fC", g_bme280_data.temperature};
        }

        // 如果湿度大于0，则添加湿度数据项
        if (g_bme280_data.humidity_pct > 0)
        {
            data_items[item_count++] = (data_item_t){"H", "%.2f%%", g_bme280_data.humidity_pct};
        }

        // 如果气压大于0，则添加气压数据项
        if (g_bme280_data.pressure_hpa > 0)
        {
            data_items[item_count++] = (data_item_t){"P", "%.2fhPa", g_bme280_data.pressure_hpa};
        }

        if (item_count > 0)
        {
            int spacing = 80;                                       // 每个数据项之间的水平间距
            int start_x = (LCD_H_RES - (item_count * spacing)) / 2; // 居中起始位置

            for (int i = 0; i < item_count; i++)
            {
                draw_string(fb, start_x + i * spacing, 208, data_items[i].name, COLOR_GRAY, COLOR_BLACK, 1);
                snprintf(str_buf, sizeof(str_buf), data_items[i].unit, data_items[i].value);
                draw_string(fb, start_x + i * spacing + 16, 208, str_buf, COLOR_CYAN, COLOR_BLACK, 1);
            }
        }
    }
}

// ==========================================
// 📊 表盘 3：系统信息表盘
// ==========================================
void draw_system_face(uint16_t *fb)
{
    draw_string(fb, 45, 10, "SYSTEM DASHBOARD", COLOR_GREEN, COLOR_BLACK, 1);
    draw_line(fb, 10, 25, 230, 25, COLOR_GRAY);

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    draw_string(fb, 45, 45, time_str, COLOR_WHITE, COLOR_BLACK, 3);

    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    draw_string(fb, 70, 85, date_str, COLOR_GRAY, COLOR_BLACK, 1);

    draw_line(fb, 10, 110, 230, 110, COLOR_GRAY);
    draw_string(fb, 10, 125, "IPv4:", COLOR_GREEN, COLOR_BLACK, 1);
    draw_string(fb, 55, 125, s_ip_address, COLOR_WHITE, COLOR_BLACK, 1);

    draw_string(fb, 10, 145, "IPv6:", COLOR_BLUE, COLOR_BLACK, 1);
    char short_ip6[40] = {0};
    strncpy(short_ip6, s_ip6_address, 39);
    draw_scrolling_string(fb, 55, 145, short_ip6, COLOR_WHITE, COLOR_BLACK, 1, 185);

    char rssi_str[32];
    snprintf(rssi_str, sizeof(rssi_str), "Wi-Fi RSSI: %d dBm", s_wifi_rssi);
    draw_string(fb, 10, 165, rssi_str, COLOR_WHITE, COLOR_BLACK, 1);
}

// 全局變數
uint32_t g_baseline = 0;
uint32_t g_trigger_threshold = 0;

void touch_button_task(void *pvParameters)
{
    touch_sensor_handle_t sens_handle = NULL;
    touch_channel_handle_t chan_handle = NULL;
    const char *TAG = "TOUCH";

    touch_sensor_sample_config_t sample_cfg[1] = {};
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens_handle));

    touch_channel_config_t chan_cfg = {0};
    chan_cfg.active_thresh[0] = 1000;
    ESP_ERROR_CHECK(touch_sensor_new_channel(sens_handle, TOUCH_PAD_CHANNEL, &chan_cfg, &chan_handle));

    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(sens_handle, &filter_cfg));

    ESP_ERROR_CHECK(touch_sensor_enable(sens_handle));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens_handle));

    ESP_LOGI(TAG, "⏳ 等待穩定... (6秒)");
    vTaskDelay(pdMS_TO_TICKS(6000));

    // 校準基準值
    uint64_t sum = 0;
    const int CALIBRATION_SAMPLES = 100;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++)
    {
        uint32_t val = 0;
        ESP_ERROR_CHECK(touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_RAW, &val));
        sum += val;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    g_baseline = sum / CALIBRATION_SAMPLES;
    ESP_LOGI(TAG, "✓ 基準值: %lu", g_baseline);

    // 「減去」百分比 (按下變小)
    g_trigger_threshold = g_baseline - (g_baseline * TOUCH_SENSITIVITY_PERCENT / 100);
    if (g_trigger_threshold < 5000)
        g_trigger_threshold = 5000;

    ESP_LOGI(TAG, "觸發閾值: %lu (下降 %d%%)", g_trigger_threshold, TOUCH_SENSITIVITY_PERCENT);

    // 重新配置硬體參數
    ESP_ERROR_CHECK(touch_sensor_stop_continuous_scanning(sens_handle));
    ESP_ERROR_CHECK(touch_sensor_disable(sens_handle));
    touch_channel_config_t new_cfg = {0};
    new_cfg.active_thresh[0] = g_trigger_threshold;
    ESP_ERROR_CHECK(touch_sensor_reconfig_channel(chan_handle, &new_cfg));
    ESP_ERROR_CHECK(touch_sensor_enable(sens_handle));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens_handle));

    ESP_LOGI(TAG, "🔘 監聽開始 (下降觸發模式)，基準: %lu", g_baseline);

    bool is_pressed = false;
    int32_t smooth_raw = (int32_t)g_baseline;
    TickType_t press_start_tick = 0;
    TickType_t last_release_tick = 0;
    int press_confirm_count = 0;
    int release_confirm_count = 0;

    while (1)
    {
        uint32_t raw_val = 0;
        if (touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_RAW, &raw_val) == ESP_OK)
        {

            // EMA 平滑濾波：3:1 權重，快速跟隨
            smooth_raw = (smooth_raw * 7 + (int32_t)raw_val) / 8;

            // 💡 施密特觸發器閾值設計：
            // threshold_press   : 下降 20% (例如 287462)，必須用力按下才觸發
            // threshold_release : 下降 10% (例如 323394)，必須極限接近基準線才算鬆開！
            int32_t threshold_press = g_baseline - (g_baseline * TOUCH_SENSITIVITY_PERCENT / 100);
            int32_t threshold_release = g_baseline - (g_baseline * (TOUCH_SENSITIVITY_PERCENT / 2) / 100);

            // 基準值緩慢跟蹤 (未按下且無劇烈下跌時)
            if (!is_pressed && smooth_raw > (int32_t)(g_baseline - (g_baseline * 5 / 100)))
            {
                if (abs((int32_t)g_baseline - smooth_raw) < (int32_t)(g_baseline * 3 / 100))
                {
                    g_baseline = (g_baseline * 99 + smooth_raw) / 100;
                }
            }

            if (!is_pressed)
            {
                // 150ms 抬手冷卻盲區，防止按鍵釋放時的電容回彈誤觸
                if (smooth_raw < threshold_press && (xTaskGetTickCount() - last_release_tick) > pdMS_TO_TICKS(150))
                {
                    press_confirm_count++;
                    // 💡 按下依然是 2 次確認 (30ms)，響應速度極快，毫無延遲感！
                    if (press_confirm_count >= 2)
                    {
                        is_pressed = true;
                        press_start_tick = xTaskGetTickCount();
                        ESP_LOGI(TAG, "🟢 按下! 基準:%lu, 讀數:%ld", g_baseline, smooth_raw);
                        press_confirm_count = 0;
                    }
                }
                else
                {
                    press_confirm_count = 0;
                }
            }
            else
            {
                if (smooth_raw > threshold_release)
                {
                    release_confirm_count++;
                    // ==========================================================
                    // 🚀 核心降維打擊：鬆開確認提升至 12 次 (180ms)！
                    // 你的 Wi-Fi 干擾脈衝最多只有 170ms，絕對無法擊穿 180ms 的防波堤！
                    // 當手指長按時，任何 110~150ms 的假鬆開都會在這裡被徹底過濾！
                    // ==========================================================
                    if (release_confirm_count >= 12)
                    {
                        is_pressed = false;
                        TickType_t current_tick = xTaskGetTickCount();
                        TickType_t press_duration = current_tick - press_start_tick;
                        last_release_tick = current_tick;
                        release_confirm_count = 0;

                        uint32_t duration_ms = pdTICKS_TO_MS(press_duration);
                        ESP_LOGI(TAG, "☝️ 鬆開! 真正按下時長: %lu ms", duration_ms);

                        // 動作攔截與分流
                        if (duration_ms < 100)
                        {
                            ESP_LOGW(TAG, "⚠️ 攔截到極短毛刺 (時長: %lu ms)，已忽略！", duration_ms);
                        }
                        else if (duration_ms > 800)
                        {
                            ESP_LOGI(TAG, "⏱️ 長按 > 800ms 成功觸發：切換表盤！");
                            app_action_switch_face();
                        }
                        else
                        {
                            ESP_LOGI(TAG, "⚡ 有效短按觸發：切換股票！");
                            app_action_switch_stock();
                        }
                    }
                }
                else
                {
                    // 💡 只要讀數在 180ms 內重新跌回了低位（干擾脈衝過去了，手指依然在按著），
                    // 立刻把鬆開計數清零，保護長按計時器永不中斷！
                    release_confirm_count = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    // 清理
    touch_sensor_stop_continuous_scanning(sens_handle);
    touch_sensor_disable(sens_handle);
    touch_sensor_del_channel(chan_handle);
    touch_sensor_del_controller(sens_handle);
}

void app_main(void)
{
    //esp_log_level_set("*", ESP_LOG_NONE); // 关闭日志
    nvs_flash_init();
    load_current_face_from_nvs();
    psa_crypto_init();
    auth_init();
    buzzer_init(); // 蜂鸣器初始化

    setenv("TZ", "CST-8", 1);
    tzset();

    graphics_init_all();
    uint16_t *fb = (uint16_t *)heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
    memset(fb, 0, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));

    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_basics();
    if (!is_device_provisioned())
    {
        start_new_network_provisioning();

        // 💡 强启后关闭 Wi-Fi 休眠，保障配网期间的 IPv6 接收
        esp_wifi_set_ps(WIFI_PS_NONE);

        draw_string(fb, 20, 50, "App: ESP SoftAP Prov", COLOR_WHITE, COLOR_BLACK, 1);
        draw_string(fb, 20, 80, "BLE PIN Code:", COLOR_BLUE, COLOR_BLACK, 1);
        draw_string(fb, 50, 110, get_ble_pin(), COLOR_RED, COLOR_BLACK, 3);

        graphics_flush_frame(fb);

        ESP_LOGI("MAIN", "等待使用者透過 APP 配网...");

        while (strcmp(s_ip_address, "未连接") == 0 || s_wifi_rssi == -999)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            refresh_wifi_status();
        }
    }
    else
    {
        draw_string(fb, 20, 100, "Connecting Wi-Fi", COLOR_GREEN, COLOR_BLACK, 2);
        graphics_flush_frame(fb);

        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();

        // 💡 强启后关闭 Wi-Fi 休眠，确保 SLAAC (IPv6) 握手不漏包
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
    wait_for_wifi_connection();

    // ==========================================
    // 连上 Wi-Fi 后，立刻清空配网画面，显示加载中
    // ==========================================
    memset(fb, 0, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    draw_string(fb, 20, 100, "Wi-Fi Connected!", COLOR_GREEN, COLOR_BLACK, 2);
    draw_string(fb, 20, 140, "Syncing Data...", COLOR_WHITE, COLOR_BLACK, 2);
    graphics_flush_frame(fb);

    // 重定向日志输出
    start_syslog_redirect();

    // ==========================================
    // 强制等待蓝牙释放内存！
    // ==========================================
    if (!is_device_provisioned())
    {
        ESP_LOGI("MAIN", "等待蓝牙配网服务关闭并释放内存...");
        vTaskDelay(pdMS_TO_TICKS(6000));
    }

    sync_time_sntp();
    bme280_init();
    load_stock_config_from_nvs();

    char last_ticker[MAX_TICKER_LEN];
    if (nvs_load_str("last_ticker", last_ticker, sizeof(last_ticker)) == ESP_OK)
    {
        for (int i = 0; i < s_stock_count; i++)
        {
            if (strcmp(s_stock_list[i].ticker, last_ticker) == 0)
            {
                s_selected_stock = i;
                break;
            }
        }
    }

    stock_api_init();
    openwrt_api_init();
    xTaskCreatePinnedToCore(&touch_button_task, "touch", 4096, NULL, 4, NULL, 1);
    start_webserver();

    // ==========================================
    // 🚀 大循环重构：加入动态多表盘分流系统
    // ==========================================
    while (1)
    {
        // 每次渲染新页面前，先清空 Framebuffer
        memset(fb, 0, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));

        switch (g_current_face)
        {
        case WATCH_FACE_STOCK:
            draw_stock_face(fb);
            break;

        case WATCH_FACE_OPENWRT:
            draw_openwrt_face(fb);
            break;

        case WATCH_FACE_SYSTEM:
            draw_system_face(fb);
            break;

        default:
            draw_stock_face(fb);
            break;
        }

        draw_fps_counter(fb);
        // 统一刷屏
        graphics_flush_frame(fb);

        vTaskDelay(pdMS_TO_TICKS(40));
    }
}