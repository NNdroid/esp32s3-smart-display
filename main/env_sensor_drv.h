#pragma once
#include <esp_err.h>

// 支持的傳感器類型枚舉
typedef enum {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_BME280,   // 溫 + 濕 + 壓 (ChipID: 0x60)
    SENSOR_TYPE_BMP280,   // 溫 + 壓     (ChipID: 0x58)
    SENSOR_TYPE_AHT20     // 溫 + 濕     (Addr: 0x38, AHT10/11/20通用)
} env_sensor_type_t;

typedef struct {
    env_sensor_type_t sensor_type;
    uint8_t i2c_addr;
    float temperature;
    float humidity_pct;
    float pressure_hpa;
    uint32_t last_update_time;
} bme280_data_t;

extern bme280_data_t g_bme280_data;

esp_err_t bme280_init(void);
esp_err_t bme280_read_measurements(void);