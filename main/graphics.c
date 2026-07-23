#include "graphics.h"
#include "app_config.h"
#include "system_utils.h"
#include "font_cn_16x16.h"
#include <stdlib.h>
#include <string.h>
#include "battery.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

static esp_lcd_panel_handle_t s_panel_handle = NULL;

// ==========================================
// 🖥️ 硬體初始化與控制
// ==========================================
void graphics_init_all(void) {
    // 1. SPI 匯流排初始化
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_NUM_SCLK, .mosi_io_num = LCD_PIN_NUM_MOSI,
        .miso_io_num = LCD_PIN_NUM_MISO, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. LCD 面板初始化
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_NUM_LCD_DC, .cs_gpio_num = LCD_PIN_NUM_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .spi_mode = 0, .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // 3. LEDC PWM 背光初始化
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 4000, 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LCD_PIN_NUM_BK_LIGHT,
        .duty           = 255, // 預設滿載
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);

    char bright_str[8] = "100";
    if (nvs_load_str("lcd_bright", bright_str, sizeof(bright_str)) == ESP_OK) {
        set_lcd_brightness(atoi(bright_str));
        ESP_LOGI("GRAPHICS", "初始化亮度应用成功: %s", bright_str);
    } else {
        set_lcd_brightness(100);
        ESP_LOGI("GRAPHICS", "使用默认亮度 100");
    }
}

void set_lcd_brightness(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void graphics_flush_frame(uint16_t *fb) {
    if (s_panel_handle) {
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, fb);
    }
}

// ==========================================
// 🔤 輕量級 5x7 點陣字庫 (ASCII 32-90)
// ==========================================
const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 Space
    {0x00,0x00,0x4F,0x00,0x00}, // 33 !
    {0x00,0x07,0x00,0x07,0x00}, // 34 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 $
    {0x23,0x13,0x08,0x64,0x62}, // 37 %
    {0x36,0x49,0x55,0x22,0x50}, // 38 &
    {0x00,0x05,0x03,0x00,0x00}, // 39 '
    {0x00,0x1C,0x22,0x41,0x00}, // 40 (
    {0x00,0x41,0x22,0x1C,0x00}, // 41 )
    {0x08,0x2A,0x1C,0x2A,0x08}, // 42 *
    {0x08,0x08,0x3E,0x08,0x08}, // 43 +
    {0x00,0x50,0x30,0x00,0x00}, // 44 ,
    {0x08,0x08,0x08,0x08,0x08}, // 45 -
    {0x00,0x60,0x60,0x00,0x00}, // 46 .
    {0x20,0x10,0x08,0x04,0x02}, // 47 /
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 0
    {0x00,0x42,0x7F,0x40,0x00}, // 49 1
    {0x42,0x61,0x51,0x49,0x46}, // 50 2
    {0x21,0x41,0x45,0x4B,0x31}, // 51 3
    {0x18,0x14,0x12,0x7F,0x10}, // 52 4
    {0x27,0x45,0x45,0x45,0x39}, // 53 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 6
    {0x01,0x71,0x09,0x05,0x03}, // 55 7
    {0x36,0x49,0x49,0x49,0x36}, // 56 8
    {0x06,0x49,0x49,0x29,0x1E}, // 57 9
    {0x00,0x36,0x36,0x00,0x00}, // 58 :
    {0x00,0x56,0x36,0x00,0x00}, // 59 ;
    {0x00,0x08,0x14,0x22,0x41}, // 60 <
    {0x14,0x14,0x14,0x14,0x14}, // 61 =
    {0x41,0x22,0x14,0x08,0x00}, // 62 >
    {0x02,0x01,0x51,0x09,0x06}, // 63 ?
    {0x32,0x49,0x79,0x41,0x3E}, // 64 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 A
    {0x7F,0x49,0x49,0x49,0x36}, // 66 B
    {0x3E,0x41,0x41,0x41,0x22}, // 67 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 D
    {0x7F,0x49,0x49,0x49,0x41}, // 69 E
    {0x7F,0x09,0x09,0x09,0x01}, // 70 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 71 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 H
    {0x00,0x41,0x7F,0x41,0x00}, // 73 I
    {0x20,0x40,0x41,0x3F,0x01}, // 74 J
    {0x7F,0x08,0x14,0x22,0x41}, // 75 K
    {0x7F,0x40,0x40,0x40,0x40}, // 76 L
    {0x7F,0x02,0x0C,0x02,0x7F}, // 77 M
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 N
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 O
    {0x7F,0x09,0x09,0x09,0x06}, // 80 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 82 R
    {0x46,0x49,0x49,0x49,0x31}, // 83 S
    {0x01,0x01,0x7F,0x01,0x01}, // 84 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 87 W
    {0x63,0x14,0x08,0x14,0x63}, // 88 X
    {0x07,0x08,0x70,0x08,0x07}, // 89 Y
    {0x61,0x51,0x49,0x45,0x43}, // 90 Z
    {0x00,0x7F,0x41,0x41,0x41}, // 91 [
    {0x02,0x04,0x08,0x10,0x20}, // 92 BACKSLASH
    {0x41,0x41,0x41,0x7F,0x00}, // 93 ]
    {0x04,0x02,0x01,0x02,0x04}, // 94 ^
    {0x40,0x40,0x40,0x40,0x40}, // 95 _
    {0x00,0x01,0x02,0x04,0x00}, // 96 `
    {0x20,0x54,0x54,0x54,0x78}, // 97 a
    {0x7F,0x48,0x44,0x44,0x38}, // 98 b
    {0x38,0x44,0x44,0x44,0x20}, // 99 c
    {0x38,0x44,0x44,0x48,0x7F}, // 100 d
    {0x38,0x54,0x54,0x54,0x18}, // 101 e
    {0x08,0x7E,0x09,0x01,0x02}, // 102 f
    {0x0C,0x52,0x52,0x52,0x3E}, // 103 g
    {0x7F,0x08,0x04,0x04,0x78}, // 104 h
    {0x00,0x44,0x7D,0x40,0x00}, // 105 i
    {0x20,0x40,0x44,0x3D,0x00}, // 106 j
    {0x7F,0x10,0x28,0x44,0x00}, // 107 k
    {0x00,0x41,0x7F,0x40,0x00}, // 108 l
    {0x78,0x04,0x18,0x04,0x78}, // 109 m
    {0x7C,0x08,0x04,0x04,0x78}, // 110 n
    {0x38,0x44,0x44,0x44,0x38}, // 111 o
    {0x7C,0x14,0x14,0x14,0x08}, // 112 p
    {0x08,0x14,0x14,0x18,0x7C}, // 113 q
    {0x7C,0x08,0x04,0x04,0x08}, // 114 r
    {0x48,0x54,0x54,0x54,0x20}, // 115 s
    {0x04,0x3F,0x44,0x40,0x20}, // 116 t
    {0x3C,0x40,0x40,0x20,0x7C}, // 117 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 118 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 119 w
    {0x44,0x28,0x10,0x28,0x44}, // 120 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 121 y
    {0x44,0x64,0x54,0x4C,0x44}, // 122 z
    {0x00,0x08,0x36,0x41,0x00}, // 123 {
    {0x00,0x00,0x7F,0x00,0x00}, // 124 |
    {0x00,0x41,0x36,0x08,0x00}, // 125 }
    {0x10,0x08,0x08,0x10,0x08}  // 126 ~
};

void draw_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        if (x0 >= 0 && x0 < LCD_H_RES && y0 >= 0 && y0 < LCD_V_RES) {
            fb[y0 * LCD_H_RES + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_circle(uint16_t *fb, int xc, int yc, int r, uint16_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                int px = xc + x;
                int py = yc + y;
                if (px >= 0 && px < LCD_H_RES && py >= 0 && py < LCD_V_RES) {
                    fb[py * LCD_H_RES + px] = color;
                }
            }
        }
    }
}

/**
 * @brief 在 Framebuffer 上繪製空心矩形
 */
void draw_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color) {
    // 繪製上邊與下邊
    for (int i = x; i < x + w; i++) {
        if (i >= 0 && i < LCD_H_RES) {
            if (y >= 0 && y < LCD_V_RES) 
                fb[y * LCD_H_RES + i] = color;
            if (y + h - 1 >= 0 && y + h - 1 < LCD_V_RES) 
                fb[(y + h - 1) * LCD_H_RES + i] = color;
        }
    }
    // 繪製左邊與右邊
    for (int j = y; j < y + h; j++) {
        if (j >= 0 && j < LCD_V_RES) {
            if (x >= 0 && x < LCD_H_RES) 
                fb[j * LCD_H_RES + x] = color;
            if (x + w - 1 >= 0 && x + w - 1 < LCD_H_RES) 
                fb[j * LCD_H_RES + (x + w - 1)] = color;
        }
    }
}

void draw_char(uint16_t *fb, int x, int y, char c, uint16_t color, uint16_t bg, uint8_t scale) {
    if (c < 32 || c > 126) c = 32; 
    uint8_t index = c - 32;
    
    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[index][i];
        for (int j = 0; j < 8; j++) {
            if (line & 0x1) {
                for(int sx = 0; sx < scale; sx++) {
                    for(int sy = 0; sy < scale; sy++) {
                        int px = x + i * scale + sx;
                        int py = y + j * scale + sy;
                        if (px >= 0 && px < LCD_H_RES && py >= 0 && py < LCD_V_RES) fb[py * LCD_H_RES + px] = color;
                    }
                }
            } else if (bg != COLOR_BLACK) { 
                for(int sx = 0; sx < scale; sx++) {
                    for(int sy = 0; sy < scale; sy++) {
                        int px = x + i * scale + sx;
                        int py = y + j * scale + sy;
                        if (px >= 0 && px < LCD_H_RES && py >= 0 && py < LCD_V_RES) fb[py * LCD_H_RES + px] = bg;
                    }
                }
            }
            line >>= 1;
        }
    }
}

void draw_string(uint16_t *fb, int x, int y, const char *str, uint16_t color, uint16_t bg, uint8_t scale) {
    while (*str) {
        char c = *str;
        if (c >= 'a' && c <= 'z') c -= 32; 
        draw_char(fb, x, y, c, color, bg, scale);
        x += (5 + 1) * scale;
        str++;
    }
}

// ==========================================
// 💡 內部專用：帶有「隧道視窗裁剪 (Clipping)」的點陣字元繪製
// ==========================================
static void draw_char_clipped(uint16_t *fb, int x, int y, char c, uint16_t color, uint16_t bg, uint8_t scale, int clip_left, int clip_right) {
    if (c < 32 || c > 126) c = 32; 
    uint8_t index = c - 32;
    
    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[index][i];
        for (int j = 0; j < 8; j++) {
            if (line & 0x1) {
                for(int sx = 0; sx < scale; sx++) {
                    for(int sy = 0; sy < scale; sy++) {
                        int px = x + i * scale + sx;
                        int py = y + j * scale + sy;
                        // 💡 核心魔法：只有在 [clip_left, clip_right) 隧道範圍內的像素才允許畫上屏幕！
                        if (px >= clip_left && px < clip_right && py >= 0 && py < LCD_V_RES) {
                            fb[py * LCD_H_RES + px] = color;
                        }
                    }
                }
            } else if (bg != COLOR_BLACK) { 
                for(int sx = 0; sx < scale; sx++) {
                    for(int sy = 0; sy < scale; sy++) {
                        int px = x + i * scale + sx;
                        int py = y + j * scale + sy;
                        if (px >= clip_left && px < clip_right && py >= 0 && py < LCD_V_RES) {
                            fb[py * LCD_H_RES + px] = bg;
                        }
                    }
                }
            }
            line >>= 1;
        }
    }
}

// ==========================================
// 💡 內部專用：帶有視窗裁剪與 CPU 算力優化的字串繪製
// ==========================================
static void draw_string_clipped(uint16_t *fb, int x, int y, const char *str, uint16_t color, uint16_t bg, uint8_t scale, int clip_left, int clip_right) {
    int char_step = (5 + 1) * scale;
    while (*str) {
        char c = *str;
        if (c >= 'a' && c <= 'z') c -= 32; 
        
        // 🚀 極限效能優化 1：如果整顆字符已經在隧道右邊緣之外，後面的字不用畫了，直接中止！
        if (x >= clip_right) break;
        
        // 🚀 極限效能優化 2：只有當字符沒有完全滑出左邊界時，才消耗 CPU 進行渲染
        if (x + char_step >= clip_left) {
            draw_char_clipped(fb, x, y, c, color, bg, scale, clip_left, clip_right);
        }
        
        x += char_step;
        str++;
    }
}

// ==========================================
// 🚀 升級版：真正做到「局部視窗循環」的跑馬燈特效！
// ==========================================
void draw_scrolling_string(uint16_t *fb, int x, int y, const char *str, 
                           uint16_t color, uint16_t bg_color, uint8_t scale, int max_width) {
    if (str == NULL || *str == '\0') return;

    int char_step = (5 + 1) * scale;
    int str_len = strlen(str);
    int total_text_width = str_len * char_step;

    // 1. 如果字串不長，直接調用普通繪製，靜態顯示
    if (total_text_width <= max_width) {
        draw_string(fb, x, y, str, color, bg_color, scale);
        return;
    }

    // 2. 💡 設定視窗裁剪隧道 (Scissor Box)
    int clip_left = x;                  // 隧道入口 (左邊界，比如 55)
    int clip_right = x + max_width;     // 隧道出口 (右邊界，比如 55 + 185 = 240)
    if (clip_right > LCD_H_RES) clip_right = LCD_H_RES; // 防溢出保護

    int gap_width = 6 * char_step; 
    int loop_period = total_text_width + gap_width;

    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    int offset = (now_ms / 30) % loop_period; 

    int draw_x1 = x - offset;
    int draw_x2 = draw_x1 + loop_period;

    // 3. 呼叫專門的「隧道裁剪版」繪製，超過 55 的字元瞬間隱形！
    draw_string_clipped(fb, draw_x1, y, str, color, bg_color, scale, clip_left, clip_right);
    draw_string_clipped(fb, draw_x2, y, str, color, bg_color, scale, clip_left, clip_right);
}

// ==========================================
// 🚀 極客 HUD：左下角實時 FPS 監控器
// ==========================================
void draw_fps_counter(uint16_t *fb) {
    static uint32_t s_last_tick = 0;
    static int s_frame_count = 0;
    static int s_current_fps = 0;

    // 1. 每次呼叫本函數，代表準備渲染新的一幀，累加計數
    s_frame_count++;

    // 2. 獲取當前系統運行時間 (毫秒)
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // 3. 每隔 1000ms (1秒)，結算一次真實物理幀率
    if (now_ms - s_last_tick >= 1000) {
        s_current_fps = s_frame_count;
        s_frame_count = 0;
        s_last_tick = now_ms;
    }

    // 4. 格式化 FPS 字串 (例如: "45 FPS")
    char fps_str[16];
    snprintf(fps_str, sizeof(fps_str), "%2d FPS", s_current_fps);

    // 5. 智能極客變色系統
    uint16_t fps_color = COLOR_GREEN;
    if (s_current_fps < 20) {
        fps_color = COLOR_RED;    // 遇到網絡阻塞或大量死循環時警示
    } else if (s_current_fps < 35) {
        fps_color = COLOR_YELLOW; // 中等幀率
    }

    // 6. 繪製在屏幕左下角
    // 螢幕解析度為 240x240，1號字體高度為 7px。坐標設在 (4, 230)，底部留出 3px 安全邊距
    draw_string(fb, 4, 230, fps_str, fps_color, COLOR_BLACK, 1);
}

/**
 * @brief 在 LCD Framebuffer 上繪製一個 16x16 中文漢字
 * @param fb 螢幕顯存指針 (uint16_t*)
 * @param x, y 螢幕左上角起始坐標
 * @param bitmap_data 32 Bytes 的中文字模資料
 * @param color 文字前景色 (RGB565)
 * @param bg_color 背景色（若填入與 color 相同的值，則代表背景透明）
 */
void draw_cn_char_16x16(uint16_t *fb, int x, int y, const uint8_t *bitmap_data, uint16_t color, uint16_t bg_color) {
    if (bitmap_data == NULL) return; // 安全防護

    int byte_idx = 0;
    bool is_transparent = (color == bg_color);

    // 逐行掃描 (共 16 行)
    for (int row = 0; row < 16; row++) {
        // 獲取當前行的左右兩個字節
        uint8_t left_byte  = bitmap_data[byte_idx++];
        uint8_t right_byte = bitmap_data[byte_idx++];

        // 1. 繪製左半邊 8 個像素 (對應 left_byte 的第 7~0 位)
        for (int col = 0; col < 8; col++) {
            int draw_x = x + col;
            int draw_y = y + row;
            
            // 螢幕邊界檢查，防止寫入越界導致記憶體崩潰
            if (draw_x >= 0 && draw_x < LCD_H_RES && draw_y >= 0 && draw_y < LCD_V_RES) {
                // 使用位元遮罩 (0x80 >> col) 檢查當前位元是否為 1
                if (left_byte & (0x80 >> col)) {
                    fb[draw_y * LCD_H_RES + draw_x] = color; // 畫前景色
                } else if (!is_transparent) {
                    fb[draw_y * LCD_H_RES + draw_x] = bg_color; // 畫背景色
                }
            }
        }

        // 2. 繪製右半邊 8 個像素 (對應 right_byte 的第 7~0 位)
        for (int col = 0; col < 8; col++) {
            int draw_x = x + 8 + col; // X 坐標向右偏移 8 像素
            int draw_y = y + row;
            
            if (draw_x >= 0 && draw_x < LCD_H_RES && draw_y >= 0 && draw_y < LCD_V_RES) {
                if (right_byte & (0x80 >> col)) {
                    fb[draw_y * LCD_H_RES + draw_x] = color;
                } else if (!is_transparent) {
                    fb[draw_y * LCD_H_RES + draw_x] = bg_color;
                }
            }
        }
    }
}

/**
 * @brief 在螢幕上列印中英混合字串 (UTF-8)
 * @example draw_cn_string(fb, 10, 20, "Wi-Fi訊號: 強", COLOR_WHITE, COLOR_BLACK);
 */
void draw_cn_string(uint16_t *fb, int x, int y, const char *str, uint16_t color, uint16_t bg_color) {
    int curr_x = x;
    int i = 0;
    int len = strlen(str);

    while (i < len) {
        if ((unsigned char)str[i] < 0x80) {
            // ASCII 字符处理...
            char ascii_str[2] = { str[i], '\0' };
            draw_string(fb, curr_x, y, ascii_str, color, bg_color, 1);
            curr_x += 8;
            i += 1;
        } 
        else {
            const uint8_t *bitmap = find_cn_bitmap_fast(&str[i]); 
            
            if (bitmap != NULL) {
                draw_cn_char_16x16(fb, curr_x, y, bitmap, color, bg_color);
            } else {
                draw_rect(fb, curr_x, y, 16, 16, COLOR_GRAY);
            }

            curr_x += 16;
            i += 3;
        }
    }
}

// ==========================================
// 🔋 繪製電池與充電狀態
// ==========================================
void draw_battery_status(uint16_t *fb, int pos, int style) {
    int pct = battery_get_percentage();
    bool is_charging = battery_is_charging();
    int x = 0, y = 0;

    if (style == 1) {
        // 数字模式
        uint16_t color = COLOR_WHITE;
        if (is_charging) color = COLOR_GREEN;
        else if (pct <= 20) color = COLOR_RED;
        
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", pct);
        
        // 动态计算文字宽高 (字体 8x8, scale=1)
        int text_w = strlen(buf) * 8;
        int text_h = 8;
        
        y = (pos == 0 || pos == 1) ? 5 : 240 - text_h - 5;
        x = (pos == 0 || pos == 2) ? 5 : 240 - text_w - 5;

        // 使用 1 倍大小的字体
        draw_string(fb, x, y, buf, color, COLOR_BLACK, 1);
        return;
    }

    // 默认图标模式 (style == 0)
    // 外框参数
    int bat_w = 22;
    int bat_h = 8;
    
    y = (pos == 0 || pos == 1) ? 5 : 240 - bat_h - 5;
    x = (pos == 0 || pos == 2) ? 5 : 240 - (bat_w + 3) - 5; // +3 留给正极触点
    
    uint16_t frame_color = COLOR_WHITE;

    // 绘制电池主体框架 (上下左右四条边)
    // 上边
    draw_line(fb, x, y, x + bat_w, y, frame_color);
    // 下边
    draw_line(fb, x, y + bat_h, x + bat_w, y + bat_h, frame_color);
    // 左边
    draw_line(fb, x, y, x, y + bat_h, frame_color);
    // 右边
    draw_line(fb, x + bat_w, y, x + bat_w, y + bat_h, frame_color);

    // 绘制电池正极触点
    draw_line(fb, x + bat_w + 1, y + 3, x + bat_w + 1, y + bat_h - 3, frame_color);
    draw_line(fb, x + bat_w + 2, y + 3, x + bat_w + 2, y + bat_h - 3, frame_color);

    // 绘制填充电量
    // 内边距 2 个像素，实际填充最大宽度 bat_w - 3
    int fill_max_w = bat_w - 3;
    int fill_w = (pct * fill_max_w) / 100;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > fill_max_w) fill_w = fill_max_w;

    uint16_t fill_color = (pct > 20) ? COLOR_GREEN : COLOR_RED;
    
    // 如果大于0，才填充
    if (fill_w > 0) {
        // 画实心矩形(通过逐行画线实现)
        for (int i = y + 2; i <= y + bat_h - 2; i++) {
            draw_line(fb, x + 2, i, x + 2 + fill_w - 1, i, fill_color);
        }
    }

    // 如果在充电，画一个黄色小闪电
    if (is_charging) {
        uint16_t lightning_color = COLOR_YELLOW;
        // 闪电中心坐标
        int cx = x + bat_w / 2;
        int cy = y + bat_h / 2;
        
        // 简单的闪电折线(缩小版适配 bat_h=8)
        draw_line(fb, cx + 2, cy - 3, cx - 1, cy, lightning_color);
        draw_line(fb, cx - 1, cy, cx + 2, cy, lightning_color);
        draw_line(fb, cx + 2, cy, cx - 2, cy + 3, lightning_color);
        
        // 加粗
        draw_line(fb, cx + 3, cy - 3, cx, cy, lightning_color);
        draw_line(fb, cx, cy, cx + 3, cy, lightning_color);
        draw_line(fb, cx + 3, cy, cx - 1, cy + 3, lightning_color);
    }
}
