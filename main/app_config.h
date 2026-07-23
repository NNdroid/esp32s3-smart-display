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

#define CHRG_STATUS_PIN GPIO_NUM_4

// =========================================================
// 🎨 ESP32 / LCD (專屬通道適配版) 終極標準調色盤宏定義
// =========================================================

// 由於你的硬體屏幕像素排列為特殊的 RBG565 (Red=高5位, Blue=中6位, Green=低5位)
// 我們定義一個專屬的巨集，將標準 RGB(888) 自動轉換為適配你硬體的 RBG565
#define RBG565(r, g, b) ((((r) & 0xF8) << 8) | (((b) & 0xFC) << 3) | (((g) & 0xF8) >> 3))

// --- 1. 基礎與經典純色 (Basic Colors) ---
#define COLOR_BLACK            RBG565(0, 0, 0)
#define COLOR_WHITE            RBG565(255, 255, 255)
#define COLOR_RED              RBG565(255, 0, 0)
#define COLOR_GREEN            RBG565(0, 255, 0)
#define COLOR_BLUE             RBG565(0, 0, 255)
#define COLOR_YELLOW           RBG565(255, 255, 0)
#define COLOR_CYAN             RBG565(0, 255, 255)
#define COLOR_MAGENTA          RBG565(255, 0, 255)

// --- 2. 股票 K線與技術指標專用色 ---
#define COLOR_ORANGE           RBG565(255, 165, 0)
#define COLOR_PURPLE           RBG565(128, 0, 128)
#define COLOR_VIOLET           RBG565(238, 130, 238)
#define COLOR_PINK             RBG565(255, 192, 203)
#define COLOR_GOLD             RBG565(255, 215, 0)
#define COLOR_BROWN            RBG565(165, 42, 42)
#define COLOR_DARK_RED         RBG565(139, 0, 0)
#define COLOR_DARK_GREEN       RBG565(0, 100, 0)
#define COLOR_DARK_BLUE        RBG565(0, 0, 139)
#define COLOR_VOL_RED          RBG565(180, 0, 0)
#define COLOR_VOL_GREEN        RBG565(0, 180, 0)
#define COLOR_VOL_BLUE         RBG565(0, 0, 180)

// --- 3. 現代 UI 灰階與網格線 ---
#define COLOR_LIGHT_GRAY       RBG565(211, 211, 211)
#define COLOR_GRAY             RBG565(128, 128, 128)
#define COLOR_DARK_GRAY        RBG565(64, 64, 64)
#define COLOR_GRID             RBG565(40, 40, 40)
#define COLOR_OPEN_LINE        RBG565(90, 90, 90)
#define COLOR_TEXT_BG          RBG565(20, 20, 80)

// --- 4. iOS / 賽博朋克風 - 高階背景暗色 ---
#define COLOR_CHARCOAL         RBG565(20, 20, 20)
#define COLOR_SLATE_BG         RBG565(25, 35, 55)
#define COLOR_NAVY_BG          RBG565(10, 20, 40)
#define COLOR_WINE_BG          RBG565(40, 0, 0)
#define COLOR_FOREST_BG        RBG565(0, 30, 0)
#define COLOR_SHADOW_RED       RBG565(40, 0, 0)
#define COLOR_SHADOW_GREEN     RBG565(0, 40, 0)
#define COLOR_SHADOW_BLUE      RBG565(0, 0, 40)

// --- 5. 亮眼霓虹與馬卡龍點綴色 ---
#define COLOR_NEON_GREEN       RBG565(57, 255, 20)
#define COLOR_NEON_PINK        RBG565(255, 20, 147)
#define COLOR_SKY_BLUE         RBG565(135, 206, 235)
#define COLOR_TEAL             RBG565(0, 128, 128)
#define COLOR_LIME             RBG565(50, 205, 50)
#define COLOR_CORAL            RBG565(255, 127, 80)

// ==========================================
// 🌍 全域變數 (供各模組共用)
// ==========================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t g_main_task_handle;

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
    WATCH_FACE_SYSTEM, 
    WATCH_FACE_OPENWRT,
    WATCH_FACE_MQTT_IMAGE,
    WATCH_FACE_MAX
} watch_face_t;

extern watch_face_t g_current_face; 

extern int g_bat_style; // 0 = 图标, 1 = 数字
extern int g_bat_pos;   // 0 = 左上, 1 = 右上, 2 = 左下, 3 = 右下

extern char g_openwrt_url[128];
extern char g_mqtt_broker_url[128];
extern char g_mqtt_topic[128];
extern char g_mqtt_username[64];
extern char g_mqtt_password[64];
