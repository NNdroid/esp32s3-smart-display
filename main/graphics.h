#pragma once
#include <stdint.h>

#ifndef LCD_H_RES
#define LCD_H_RES 240
#define LCD_V_RES 240
#endif

// --- 硬體控制介面 ---
void graphics_init_all(void);
void set_lcd_brightness(int percent);
void graphics_flush_frame(uint16_t *fb);

// --- 繪圖演算法介面 ---
void draw_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t color);
void draw_circle(uint16_t *fb, int xc, int yc, int r, uint16_t color);
void draw_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
void draw_char(uint16_t *fb, int x, int y, char c, uint16_t color, uint16_t bg, uint8_t scale);
void draw_string(uint16_t *fb, int x, int y, const char *str, uint16_t color, uint16_t bg, uint8_t scale);
void draw_scrolling_string(uint16_t *fb, int x, int y, const char *str, uint16_t color, uint16_t bg_color, uint8_t scale, int max_width);
void draw_fps_counter(uint16_t *fb);
void draw_cn_char_16x16(uint16_t *fb, int x, int y, const uint8_t *bitmap_data, uint16_t color, uint16_t bg_color);
void draw_cn_string(uint16_t *fb, int x, int y, const char *str, uint16_t color, uint16_t bg_color);