#pragma once
#include <stdint.h>

extern const uint8_t g_cn_font_16x16[][32];
extern const int g_cn_font_count;

// 汉字映射表
typedef struct {
    const char *utf8;      // UTF-8编码（3字节）
    uint16_t unicode;      // Unicode编码
    const uint8_t *data;   // 字模数据指针
} CN_Font_Map;

/**
 * @brief 在對照表中尋找 UTF-8 漢字的點陣資料
 * @return 找到則回傳 32 Bytes 指針，找不到則回傳 NULL
 */
const uint8_t* find_cn_bitmap(const char *utf8_char);

/**
 * @brief 在對照表中尋找 UTF-8 漢字的點陣資料（快速二分搜尋）
 * @return 找到則回傳 32 Bytes 指針，找不到則回傳 NULL
 */
const uint8_t* find_cn_bitmap_fast(const char *utf8_char);