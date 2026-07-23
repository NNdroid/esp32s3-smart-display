#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化鋰電池 ADC 檢測模組
 * 
 * 💡 默認配置：
 * - 引腳：IO7 (ADC1_CHANNEL_6)
 * - 衰減：ADC_ATTEN_DB_12 (支持高達 ~3.3V 輸入)
 * - 校準：自動加載 ESP32-S3 芯片內部 eFuse 曲線校準
 * 
 * @return ESP_OK 初始化成功
 */
esp_err_t battery_init(void);

/**
 * @brief 獲取當前鋰電池的真實電壓（毫伏 mV）
 * 
 * 💡 內部自動執行 10 次採樣取平均，並通過 1:2 分壓電阻係數還原真實電壓。
 * 
 * @return 毫伏數值 (例如：4150 代表 4.15V)
 */
int battery_get_voltage_mv(void);

/**
 * @brief 獲取當前鋰電池的電量百分比 (0% ~ 100%)
 * 
 * 💡 內置 EMA 平滑濾波，有效抑制 Wi-Fi 射頻發射與喇叭高音帶來的電壓瞬時跌落。
 * 
 * @return 0 ~ 100 的百分比數值
 */
int battery_get_percentage(void);

/**
 * @brief 獲取當前是否正在充電（純軟件估算）
 * 
 * 💡 由於沒有硬件引腳，通過電壓變化趨勢或高電壓閾值來估算充電狀態
 * 
 * @return true 表示可能在充電，false 表示未充电
 */
bool battery_is_charging(void);

/**
 * @brief 釋放 ADC 資源（在休眠前可調用）
 */
void battery_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_H