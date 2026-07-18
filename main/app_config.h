#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ==========================================
// ⚙️ 硬體引腳與巨集配置
// ==========================================
#define LCD_HOST               SPI2_HOST
#define LCD_PIN_NUM_SCLK       41
#define LCD_PIN_NUM_MOSI       42
#define LCD_PIN_NUM_MISO       -1
#define LCD_PIN_NUM_LCD_DC     39
#define LCD_PIN_NUM_LCD_RST    1
#define LCD_PIN_NUM_LCD_CS     40
#define LCD_PIN_NUM_BK_LIGHT   16
#define TOUCH_PAD_CHANNEL      6
#define TOUCH_DEBOUNCE_MS      600
#define TOUCH_HOLD_MS          250

#define LCD_PIXEL_CLOCK_HZ     (40 * 1000 * 1000)
#define LCD_H_RES              240
#define LCD_V_RES              240
#define BME280_I2C_SCL_IO      18
#define BME280_I2C_SDA_IO      8
#define BME280_I2C_CLOCK_HZ    100000
#define BUZZER_PIN             2

// =========================================================
// 🎨 ESP32 / LCD (專屬通道適配版) 終極標準調色盤宏定義
// =========================================================

// --- Tier 2：中亮色 (成交量柱狀圖專用 - 不干擾主線，但清晰可見) ---
#define COLOR_VOL_RED          0xB000  // 70% 亮度紅 (中紅)
#define COLOR_VOL_GREEN        0x0016  // 70% 亮度綠 (中綠) [完美契合你的硬體]
#define COLOR_VOL_BLUE         0x0500  // 70% 亮度藍 (中藍) [完美契合你的硬體]

// --- Tier 3：極暗微光色 (分時圖背景網點陰影 / 副圖底板專用) ---
#define COLOR_SHADOW_RED       0x3800  // 22% 極暗紅 (僅作淡淡的紅暈底色)
#define COLOR_SHADOW_GREEN     0x0007  // 22% 極暗綠 (僅作淡淡的綠暈底色) [完美契合你的硬體]
#define COLOR_SHADOW_BLUE      0x0180  // 22% 極暗藍 (適合副圖底板或微光網格) [完美契合你的硬體]

// --- 1. 基礎與經典純色 (Basic Colors - 嚴格適配你的通道) ---
#define COLOR_BLACK            0x0000  // 純黑 (背景/屏幕清除)
#define COLOR_WHITE            0xFFFF  // 純白 (主標題/重要文本)
#define COLOR_RED              0xF800  // 純紅 (歐美 K線陰線/警告提示)
#define COLOR_GREEN            0x001F  // 純綠 (歐美 K線陽線/成功提示) [完美契合你的硬體]
#define COLOR_BLUE             0x07E0  // 純藍 (藍色狀態/IPv6鏈示) [完美契合你的硬體]
#define COLOR_YELLOW           0xF81F  // 純黃 (重要數值/日期線/均線) [重新映射]
#define COLOR_CYAN             0x07FF  // 青色/藍綠 (HUD邊框/氣象數據)
#define COLOR_MAGENTA          0xF81F  // 洋紅/紫紅 (特殊標籤/均線)

// --- 2. 股票 K線與技術指標專用色 (Financial & MA Lines) ---
#define COLOR_ORANGE           0xFC1F  // 亮橘色 (極佳！適合 MA5 均線) [重新映射]
#define COLOR_PURPLE           0x8010  // 深紫色 (適合 MA10 均線)
#define COLOR_VIOLET           0x901A  // 紫羅蘭 (亮紫，適合 MA20 均線)
#define COLOR_PINK             0xFC10  // 粉紅色 (適合 MA30/MA60 均線) [重新映射]
#define COLOR_GOLD             0xFC0F  // 金黃色 (高亮焦點代碼/金牌提示) [重新映射]
#define COLOR_BROWN            0xA145  // 棕色 (副圖分割或次要圖表線)
#define COLOR_DARK_RED         0x7800  // 暗紅 (K線成交量柱-跌/陰影底色)
#define COLOR_DARK_GREEN       0x000F  // 暗綠 (K線成交量柱-漲/陰影底色) [完美契合你的硬體]
#define COLOR_DARK_BLUE        0x0400  // 暗藍 (副圖底色) [完美契合你的硬體]

// --- 3. 現代 UI 灰階與網格線 (Grayscale & Grids) ---
#define COLOR_LIGHT_GRAY       0xD6BA  // 淺灰 (次要文本/副標題)
#define COLOR_GRAY             0x6B6D  // 中灰 (一般標籤/非活動按鍵)
#define COLOR_DARK_GRAY        0x3186  // 暗灰 (次要網格/刻度線)
#define COLOR_GRID             0x2945  // 深度背景網格線 (極暗灰，不搶視覺) [使用你的原有數值]
#define COLOR_OPEN_LINE        0x5A8B  // 昨收基準虛線 [使用你的原有數值]
#define COLOR_TEXT_BG          0x1082  // 文本選中背景 [使用你的原有數值]

// --- 4. iOS / 賽博朋克風 - 高階背景暗色 (Dark Cards & Backgrounds) ---
#define COLOR_CHARCOAL         0x1122  // 炭黑 (比純黑略暖，極佳的屏幕底色)
#define COLOR_SLATE_BG         0x1947  // 板岩暗藍 (經典 iOS 卡片背景，如 OpenWrt 模塊底板)
#define COLOR_NAVY_BG          0x0813  // 深海軍藍 (適合系統信息表盤背景)
#define COLOR_WINE_BG          0x2800  // 暗酒紅背景 (適合報錯或警告彈窗底色)
#define COLOR_FOREST_BG        0x000A  // 暗墨綠背景 (適合網絡連接正常時的底板) [重新映射]

// --- 5. 亮眼霓虹與馬卡龍點綴色 (Neon & Accent Colors) ---
#define COLOR_NEON_GREEN       0x03FF  // 螢光綠 (超亮！適合 Wi-Fi/Ping 極佳狀態) [重新映射]
#define COLOR_NEON_PINK        0xF810  // 螢光粉 (適合警報/高亮焦點) [重新映射]
#define COLOR_SKY_BLUE         0x05FF  // 天空藍 (天氣溫度/濕度顯示) [重新映射]
#define COLOR_TEAL             0x0410  // 深青/湖水綠 (副標題裝飾線)
#define COLOR_LIME             0x051F  // 萊姆綠 (活潑的提示色) [重新映射]
#define COLOR_CORAL            0xF815  // 珊瑚紅 (溫和的警告色，不像純紅那麼刺眼) [重新映射]

// ==========================================
// 🌍 全域變數 (供各模組共用)
// ==========================================
extern int s_stock_count;
extern volatile int s_selected_stock;

extern char s_ip_address[32];
extern char s_ip6_address[64];
extern int s_wifi_rssi;

// ==========================================
// 全局表盤枚舉與變數
// ==========================================
typedef enum {
    WATCH_FACE_STOCK = 0,
    WATCH_FACE_OPENWRT,
    WATCH_FACE_SYSTEM
} watch_face_t;

extern watch_face_t g_current_face; 