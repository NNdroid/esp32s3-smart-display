#include "stock_api.h"
#include "system_utils.h"
#include "app_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> // 💡 必须引入，用于 localtime 和 strftime
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "STOCK_API";

// 全域变量定义区
volatile chart_mode_t g_chart_mode = CHART_MODE_KLINE; // 默认展示 K线
kline_data_t kline_data[MAX_KLINE_COUNT];
int kline_data_count = 0;

float stock_prices[MAX_POINTS];
uint32_t stock_volumes[MAX_POINTS];
int data_count = 0;
float current_open_price = 0.0f;
stock_config_t s_stock_list[MAX_STOCKS];
int s_stock_count = 0;
volatile int s_selected_stock = 0;

void *s_fetch_task_handle = NULL;
static StackType_t *s_stock_task_stack = NULL;
static StaticTask_t s_stock_task_tcb;

static void detect_market_code(stock_config_t *stock)
{
    if (strstr(stock->ticker, ".HK") || strstr(stock->ticker, ".hk"))
    {
        strncpy(stock->market, "HK", sizeof(stock->market) - 1);
    }
    else if (strstr(stock->ticker, ".SS") || strstr(stock->ticker, ".ss") ||
             strstr(stock->ticker, ".SZ") || strstr(stock->ticker, ".sz"))
    {
        strncpy(stock->market, "CN", sizeof(stock->market) - 1);
    }
    else
    {
        strncpy(stock->market, "US", sizeof(stock->market) - 1);
    }
    stock->market[sizeof(stock->market) - 1] = '\0';
}

static void reset_stock_defaults(void)
{
    s_stock_count = 5;
    strncpy(s_stock_list[0].ticker, "AAPL", sizeof(s_stock_list[0].ticker) - 1);
    strncpy(s_stock_list[1].ticker, "GOOGL", sizeof(s_stock_list[1].ticker) - 1);
    strncpy(s_stock_list[2].ticker, "NVDA", sizeof(s_stock_list[2].ticker) - 1);
    strncpy(s_stock_list[3].ticker, "0700.HK", sizeof(s_stock_list[3].ticker) - 1);
    strncpy(s_stock_list[4].ticker, "600000.SS", sizeof(s_stock_list[4].ticker) - 1);
    for (int i = 0; i < s_stock_count; i++)
    {
        s_stock_list[i].display_name[0] = '\0';
        s_stock_list[i].market[0] = '\0';
        detect_market_code(&s_stock_list[i]);
    }
    g_chart_mode = CHART_MODE_LINE; // 默认展示折线图
}

void load_stock_config_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("stock_cfg", NVS_READONLY, &handle) != ESP_OK)
    {
        reset_stock_defaults();
        return;
    }
    uint8_t count = 0;
    if (nvs_get_u8(handle, "count", &count) != ESP_OK || count == 0)
    {
        nvs_close(handle);
        reset_stock_defaults();
        return;
    }
    s_stock_count = count > MAX_STOCKS ? MAX_STOCKS : count;
    for (int i = 0; i < s_stock_count; i++)
    {
        char key[16];
        snprintf(key, sizeof(key), "stock%d", i);
        size_t len = sizeof(s_stock_list[i].ticker);
        if (nvs_get_str(handle, key, s_stock_list[i].ticker, &len) != ESP_OK)
            s_stock_list[i].ticker[0] = '\0';
        s_stock_list[i].display_name[0] = '\0';
        s_stock_list[i].market[0] = '\0';
        if (s_stock_list[i].ticker[0] != '\0')
            detect_market_code(&s_stock_list[i]);
    }
    g_chart_mode = nvs_load_u8("chart_mode", (uint8_t *)&g_chart_mode) == ESP_OK ? g_chart_mode : CHART_MODE_LINE;
    nvs_close(handle);
}

void save_stock_config_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open("stock_cfg", NVS_READWRITE, &handle) != ESP_OK)
        return;
    nvs_set_u8(handle, "count", (uint8_t)s_stock_count);
    for (int i = 0; i < s_stock_count; i++)
    {
        char key[16];
        snprintf(key, sizeof(key), "stock%d", i);
        nvs_set_str(handle, key, s_stock_list[i].ticker);
    }
    nvs_commit(handle);
    nvs_close(handle);
}

static bool parse_stock_display_name_from_json_str(const char *json_str, char *out, size_t out_size)
{
    bool success = false;
    cJSON *root = cJSON_Parse(json_str);
    if (root)
    {
        cJSON *chart = cJSON_GetObjectItem(root, "chart");
        if (chart)
        {
            cJSON *res = cJSON_GetObjectItem(chart, "result");
            if (cJSON_IsArray(res) && cJSON_GetArraySize(res) > 0)
            {
                cJSON *meta = cJSON_GetObjectItem(cJSON_GetArrayItem(res, 0), "meta");
                cJSON *name = cJSON_GetObjectItem(meta, "shortName");
                if (name && cJSON_IsString(name))
                {
                    strncpy(out, name->valuestring, out_size - 1);
                    out[out_size - 1] = '\0';
                    success = true;
                }
            }
        }
        cJSON_Delete(root);
    }
    return success;
}

static void parse_yahoo_finance_json(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root)
        return;

    // 1. 链式快速安全提取到 result[0]
    cJSON *chart = cJSON_GetObjectItem(root, "chart");
    cJSON *result_arr = chart ? cJSON_GetObjectItem(chart, "result") : NULL;
    if (!cJSON_IsArray(result_arr) || cJSON_GetArraySize(result_arr) <= 0)
    {
        cJSON_Delete(root);
        return;
    }
    cJSON *result0 = cJSON_GetArrayItem(result_arr, 0);

    // 2. 提取时间戳与基础交易数据节点
    cJSON *timestamp_arr = cJSON_GetObjectItem(result0, "timestamp");
    cJSON *indicators = cJSON_GetObjectItem(result0, "indicators");
    cJSON *quote_arr = indicators ? cJSON_GetObjectItem(indicators, "quote") : NULL;
    if (!cJSON_IsArray(quote_arr) || cJSON_GetArraySize(quote_arr) <= 0)
    {
        cJSON_Delete(root);
        return;
    }
    cJSON *quote0 = cJSON_GetArrayItem(quote_arr, 0);
    cJSON *close_arr = cJSON_GetObjectItem(quote0, "close");

    int total_points = cJSON_IsArray(close_arr) ? cJSON_GetArraySize(close_arr) : 0;
    if (total_points <= 0)
    {
        cJSON_Delete(root);
        return;
    }

    if (g_chart_mode == CHART_MODE_LINE)
    {
        // --- 【折线分时图模式】 ---
        cJSON *volume_arr = cJSON_GetObjectItem(quote0, "volume");
        data_count = 0;
        int start_idx = (total_points > MAX_POINTS) ? (total_points - MAX_POINTS) : 0;

        for (int i = start_idx; i < total_points && data_count < MAX_POINTS; i++)
        {
            cJSON *p_item = cJSON_GetArrayItem(close_arr, i);
            cJSON *v_item = cJSON_GetArrayItem(volume_arr, i);
            if (p_item && !cJSON_IsNull(p_item) && v_item && !cJSON_IsNull(v_item))
            {
                stock_prices[data_count] = (float)p_item->valuedouble;
                stock_volumes[data_count] = (uint32_t)v_item->valuedouble;
                data_count++;
            }
        }
    }
    else
    {
        // --- 【15分钟 K线图模式】 ---
        cJSON *open_arr = cJSON_GetObjectItem(quote0, "open");
        cJSON *high_arr = cJSON_GetObjectItem(quote0, "high");
        cJSON *low_arr = cJSON_GetObjectItem(quote0, "low");

        kline_data_count = 0;
        int start_idx = (total_points > MAX_KLINE_COUNT) ? (total_points - MAX_KLINE_COUNT) : 0;

        for (int i = start_idx; i < total_points && kline_data_count < MAX_KLINE_COUNT; i++)
        {
            cJSON *o = cJSON_GetArrayItem(open_arr, i);
            cJSON *h = cJSON_GetArrayItem(high_arr, i);
            cJSON *l = cJSON_GetArrayItem(low_arr, i);
            cJSON *c = cJSON_GetArrayItem(close_arr, i);
            cJSON *t = cJSON_IsArray(timestamp_arr) ? cJSON_GetArrayItem(timestamp_arr, i) : NULL;

            // 遇到无效/停牌/空缺数据则直接跳过，保护渲染器不崩溃
            if (!o || !h || !l || !c || cJSON_IsNull(o) || cJSON_IsNull(c))
                continue;

            // 💡 使用指针引用，替换冗长的数组索引
            kline_data_t *k = &kline_data[kline_data_count];
            k->open = (float)o->valuedouble;
            k->high = (float)h->valuedouble;
            k->low = (float)l->valuedouble;
            k->close = (float)c->valuedouble;

            // 💡 使用 valuedouble 防止超大时间戳溢出
            if (t && !cJSON_IsNull(t))
            {
                time_t local_t = (time_t)t->valuedouble;
                struct tm *timeinfo = localtime(&local_t);
                strftime(k->time, sizeof(k->time), "%H:%M", timeinfo);
                strftime(k->date, sizeof(k->date), "%m/%d", timeinfo);
            }
            else
            {
                strcpy(k->time, "--:--");
                strcpy(k->date, "--/--");
            }
            kline_data_count++;
        }
    }
    cJSON_Delete(root);
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer = NULL;
    static int output_len = 0;

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (output_buffer == NULL)
        {
            output_buffer = malloc(evt->data_len + 1);
            output_len = 0;
        }
        else
        {
            char *tmp = realloc(output_buffer, output_len + evt->data_len + 1);
            if (tmp == NULL)
            {
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
                return ESP_FAIL;
            }
            output_buffer = tmp;
        }
        if (output_buffer == NULL)
            return ESP_FAIL;
        memcpy(output_buffer + output_len, evt->data, evt->data_len);
        output_len += evt->data_len;
        output_buffer[output_len] = '\0';
        break;

    case HTTP_EVENT_ON_FINISH:
        if (output_buffer != NULL)
        {
            int idx = s_selected_stock;
            if (idx < s_stock_count)
            {
                stock_config_t *stock = &s_stock_list[idx];
                if (stock->display_name[0] == '\0')
                {
                    parse_stock_display_name_from_json_str(output_buffer, stock->display_name, sizeof(stock->display_name));
                }
            }

            // 解析昨收价（利用 strlen 免除硬编码偏移计算）
            const char *target = "\"chartPreviousClose\":";
            char *prev_close_ptr = strstr(output_buffer, target);
            if (prev_close_ptr)
            {
                current_open_price = strtof(prev_close_ptr + strlen(target), NULL);
            }

            // 调用干净精简的 JSON 解析函数
            parse_yahoo_finance_json(output_buffer);

            free(output_buffer);
            output_buffer = NULL;
            output_len = 0;
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
            output_len = 0;
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}

static void fetch_stock_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Stock fetch task running on Core [%d]", xPortGetCoreID());
    while (1)
    {
        if (g_current_face != WATCH_FACE_STOCK)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (s_stock_count <= 0)
        {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
            continue;
        }
        int idx = s_selected_stock;
        if (idx >= s_stock_count)
        {
            idx = 0;
            s_selected_stock = 0;
        }

        stock_config_t *stock = &s_stock_list[idx];
        if (stock->market[0] == '\0')
        {
            detect_market_code(stock);
        }

        // 一行代码根据图表模式切换参数
        const char *interval_str = (g_chart_mode == CHART_MODE_LINE) ? "interval=1m&range=1d" : "interval=15m&range=5d";

        char api_url[256];
        snprintf(api_url, sizeof(api_url),
                 "https://query1.finance.yahoo.com/v8/finance/chart/%s?%s&includePrePost=true",
                 stock->ticker, interval_str);
        ESP_LOGI(TAG, "Fetching stock data for %s (%s) from URL: %s", stock->ticker, stock->market, api_url);

        esp_http_client_config_t config = {
            .url = api_url,
            .event_handler = _http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client != NULL)
        {
            esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0");
            esp_http_client_perform(client);
            esp_http_client_cleanup(client);
        }
        // ==========================================================
        // 🚀 動態休眠時間配置 (Smart Polling Interval)
        // ==========================================================
        // 1. 如果是折線分時圖 (LINE)：追求實時性，每 15 秒更新一次
        // 2. 如果是 15分鐘 K線圖 (KLINE)：追求省電與穩定，每 90 秒 (1分半) 更新一次
        uint32_t poll_interval_ms = (g_chart_mode == CHART_MODE_LINE) ? 15000 : 90000;

        uint32_t notify_val = 0;
        // 讓線程進入休眠，直到時間到期，或者被觸摸按鍵長按/短按強制喚醒！
        xTaskNotifyWait(0, ULONG_MAX, &notify_val, pdMS_TO_TICKS(poll_interval_ms));
    }
}

void stock_api_init(void)
{
    load_stock_config_from_nvs();
    ESP_LOGI(TAG, "Stock configuration loaded from NVS. Total stocks: %d", s_stock_count);

    s_stock_task_stack = (StackType_t *)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);

    if (s_stock_task_stack != NULL)
    {
        s_fetch_task_handle = xTaskCreateStaticPinnedToCore(
            fetch_stock_task,
            "stock_fetch",
            16384 / sizeof(StackType_t),
            NULL,
            5,
            s_stock_task_stack,
            &s_stock_task_tcb,
            1);
        ESP_LOGI(TAG, "Static stock fetch task successfully pinned to Core 1 in PSRAM!");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for stock task! Fallback to internal RAM.");
        xTaskCreatePinnedToCore(fetch_stock_task, "stock_fetch", 16384, NULL, 5, (TaskHandle_t *)&s_fetch_task_handle, 1);
    }
}