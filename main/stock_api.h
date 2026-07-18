#pragma once
#include "app_config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_STOCKS             8
#define MAX_POINTS             LCD_H_RES
#define MAX_TICKER_LEN         32
#define MAX_NAME_LEN           64
#define MAX_KLINE_COUNT        50 // 💡 统一定义 240 宽屏幕最多容纳的 K 线根数

// 图表模式枚举
typedef enum {
    CHART_MODE_LINE = 0, // 折线分时图
    CHART_MODE_KLINE     // 15分钟 K线图
} chart_mode_t;

// 15分钟 K线结构体
typedef struct {
    float open;
    float high;
    float low;
    float close;
    char time[6]; // 例如 "14:30"
    char date[6]; // 例如 "07/15"
} kline_data_t;

// 声明全局变量，供 UI 绘图和 API 任务共用
extern volatile chart_mode_t g_chart_mode;
extern kline_data_t kline_data[MAX_KLINE_COUNT];
extern int kline_data_count;

typedef struct {
    char ticker[MAX_TICKER_LEN];
    char display_name[MAX_NAME_LEN];
    char market[4];
} stock_config_t;

extern float stock_prices[];
extern uint32_t stock_volumes[];
extern int data_count;
extern float current_open_price;
extern stock_config_t s_stock_list[];
extern int s_stock_count;
extern volatile int s_selected_stock;

// 💡 暴露任务句柄，方便触摸/按键切换图表或表盘时，通过 xTaskNotifyGive() 瞬间唤醒任务
extern void *s_fetch_task_handle;

// 功能函数接口
void load_stock_config_from_nvs(void);
void save_stock_config_to_nvs(void);

/**
 * @brief 终极封装：初始化股票模块
 * 自动从 NVS 读取配置，并在 PSRAM 中分配 16KB 堆栈，将任务绑定至 Core 1 运行
 */
void stock_api_init(void);

#ifdef __cplusplus
}
#endif