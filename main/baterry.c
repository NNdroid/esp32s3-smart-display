#include "battery.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "BATTERY";

#ifndef CHRG_STATUS_PIN
#define CHRG_STATUS_PIN GPIO_NUM_4
#endif

// ==================== 🛠️ 硬件參數配置區 ====================
#define BAT_ADC_UNIT          ADC_UNIT_1
#define BAT_ADC_CHAN          ADC_CHANNEL_6    // IO7 對應 ADC1 的 Channel 6
#define BAT_ADC_ATTEN         ADC_ATTEN_DB_12  // IDF v5.x 使用 DB_12 測量 0~3.3V (舊版為 DB_11)

#define BAT_VOLTAGE_DIVIDER   2.0f             // 兩個等值電阻 (如 100k+100k) 串聯分壓，倍率為 2
#define BAT_MAX_MV            4150             // 4.15V 以上視為 100% 滿電 (留 50mV 餘量)
#define BAT_MIN_MV            3300             // 3.30V 以下視為 0% 沒電 (再低 3.3V LDO 將無法穩壓)
#define SAMPLE_COUNT          10               // 每次讀取的採樣均值次數
// ==========================================================

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_do_calibration = false;
static int s_smoothed_mv = 0;                  // EMA 平滑後的電壓緩存

// 內部私有函數：初始化 eFuse 曲線校準
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "正在加載 eFuse Curve Fitting 校準方案...");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "正在加載 eFuse Line Fitting 校準方案...");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ ADC 硬件校準方案加載成功！");
    } else {
        ESP_LOGW(TAG, "⚠️ 未找到 eFuse 校準數據，將使用原始估算數值 (精度可能略遜)！");
    }
    return calibrated;
}

esp_err_t battery_init(void) {
    if (s_adc_handle != NULL) return ESP_OK;

    // 1. 初始化 ADC 單次讀取單元
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = BAT_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &s_adc_handle));

    // 2. 配置 ADC 通道與衰減
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 默認 12-bit (0~4095)
        .atten = BAT_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BAT_ADC_CHAN, &config));

    // 3. 獲取校準句柄
    s_do_calibration = adc_calibration_init(BAT_ADC_UNIT, BAT_ADC_CHAN, BAT_ADC_ATTEN, &s_cali_handle);

    // 4. 初始化 GPIO4 作為充電狀態檢測引腳 (內部上拉)
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CHRG_STATUS_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);

    // 5. 上電初次讀取，初始化平滑緩存
    s_smoothed_mv = battery_get_voltage_mv();
    ESP_LOGI(TAG, "🔋 電池檢測模組初始化完成 (ADC: IO7, CHRG: IO4)，當前初始電壓: %d mV (%d%%)", 
             s_smoothed_mv, battery_get_percentage());

    return ESP_OK;
}

int battery_get_voltage_mv(void) {
    if (s_adc_handle == NULL) return 0;

    long sum_raw = 0;
    int raw_val = 0;

    // 連續採樣 10 次，剔除高頻尖峰噪聲
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (adc_oneshot_read(s_adc_handle, BAT_ADC_CHAN, &raw_val) == ESP_OK) {
            sum_raw += raw_val;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    int avg_raw = (int)(sum_raw / SAMPLE_COUNT);

    int pin_mv = 0;
    // 將原始 ADC 轉換為毫伏 (mV)
    if (s_do_calibration && s_cali_handle != NULL) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_cali_handle, avg_raw, &pin_mv));
    } else {
        // 兜底公式：假設 3.3V 參考電壓，12-bit 分辨率 (4095)
        pin_mv = (avg_raw * 3300) / 4095;
    }

    // 乘以分壓倍率，還原為真正的大電池端電壓
    int real_battery_mv = (int)(pin_mv * BAT_VOLTAGE_DIVIDER);
    return real_battery_mv;
}

static int64_t s_last_battery_read_time = 0;

int battery_get_percentage(void) {
    int64_t now = esp_timer_get_time();
    // 💡 性能优化：避免每帧调用时都造成 20ms 的读取延迟，限制为每秒读取一次
    if (s_last_battery_read_time == 0 || (now - s_last_battery_read_time) > 1000000) {
        s_last_battery_read_time = now;
        int current_mv = battery_get_voltage_mv();
        if (current_mv > 0) {
            // 💡 核心黑科技：EMA 慢速平滑濾波 (權重 8:2)
            if (s_smoothed_mv == 0) {
                s_smoothed_mv = current_mv;
            } else {
                s_smoothed_mv = (s_smoothed_mv * 8 + current_mv * 2) / 10;
            }
        }
    }
    
    if (s_smoothed_mv == 0) return 0;

    // 區間限制 (Clamp)
    if (s_smoothed_mv >= BAT_MAX_MV) return 100;
    if (s_smoothed_mv <= BAT_MIN_MV) return 0;

    // 線性映射計算百分比
    int pct = (s_smoothed_mv - BAT_MIN_MV) * 100 / (BAT_MAX_MV - BAT_MIN_MV);
    return pct;
}

bool battery_is_charging(void) {
    // 透過硬體 IO4 精準檢測：0 = 充電中，1 = 未充電
    return (gpio_get_level(CHRG_STATUS_PIN) == 0);
}

void battery_deinit(void) {
    if (s_adc_handle != NULL) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
    if (s_do_calibration && s_cali_handle != NULL) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(s_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(s_cali_handle);
#endif
        s_cali_handle = NULL;
    }
    ESP_LOGI(TAG, "🛑 電池檢測模組已停止並釋放資源");
}