#include "env_sensor_drv.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "ENV_SENSOR";

bme280_data_t g_bme280_data = {
    .sensor_type = SENSOR_TYPE_NONE,
    .i2c_addr = 0x00,
    .temperature = 0.0f,
    .humidity_pct = 0.0f,
    .pressure_hpa = 0.0f,
    .last_update_time = 0,
};

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_sensor_dev = NULL;

// BME/BMP280 校準參數結構體
typedef struct {
    uint16_t dig_T1; int16_t dig_T2; int16_t dig_T3;
    uint16_t dig_P1; int16_t dig_P2; int16_t dig_P3; int16_t dig_P4;
    int16_t dig_P5; int16_t dig_P6; int16_t dig_P7; int16_t dig_P8; int16_t dig_P9;
    uint8_t dig_H1; int16_t dig_H2; uint8_t dig_H3; int16_t dig_H4; int16_t dig_H5; int8_t dig_H6;
} bme280_calib_data_t;

static bme280_calib_data_t s_calib;

// 底層 I2C 讀寫封裝
static esp_err_t i2c_read_bytes(uint8_t reg, uint8_t *data, size_t len) {
    esp_err_t err = i2c_master_transmit(s_sensor_dev, &reg, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;
    return i2c_master_receive(s_sensor_dev, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_write_byte(uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    return i2c_master_transmit(s_sensor_dev, buffer, 2, pdMS_TO_TICKS(100));
}

// ==========================================
// AHT10 / AHT11 / AHT20 專用初始化與讀取模組
// ==========================================
static esp_err_t aht20_init_sensor(void) {
    vTaskDelay(pdMS_TO_TICKS(40)); // 上電軟啟動時間
    uint8_t init_cmd[3] = {0xBE, 0x08, 0x00}; // AHT20 初始化命令
    i2c_master_transmit(s_sensor_dev, init_cmd, 3, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "✅ AHT系列 (AHT10/11/20) 溫濕度傳感器加載成功!");
    return ESP_OK;
}

static esp_err_t aht20_read_data(float *temp, float *hum) {
    uint8_t trigger_cmd[3] = {0xAC, 0x33, 0x00}; // 觸發測量命令
    if (i2c_master_transmit(s_sensor_dev, trigger_cmd, 3, pdMS_TO_TICKS(100)) != ESP_OK) return ESP_FAIL;
    
    vTaskDelay(pdMS_TO_TICKS(80)); // 等待測量完成 (大約需 75ms)
    
    uint8_t data[6];
    if (i2c_master_receive(s_sensor_dev, data, 6, pdMS_TO_TICKS(100)) != ESP_OK) return ESP_FAIL;
    
    if ((data[0] & 0x80) == 0x80) return ESP_ERR_NOT_FINISHED; // 忙碌中

    uint32_t raw_hum = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((data[3] >> 4) & 0x0F);
    uint32_t raw_temp = (((uint32_t)(data[3] & 0x0F)) << 16) | ((uint32_t)data[4] << 8) | data[5];

    *hum = (raw_hum * 100.0f) / 1048576.0f;
    *temp = ((raw_temp * 200.0f) / 1048576.0f) - 50.0f;
    return ESP_OK;
}

// ==========================================
// 總線初始化與傳感器自適應掃描
// ==========================================
esp_err_t bme280_init(void) {
    if (s_sensor_dev != NULL) return ESP_OK;

    i2c_master_bus_config_t buscfg = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = BME280_I2C_SCL_IO,
        .sda_io_num = BME280_I2C_SDA_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // 開啟內部上拉，增強通訊穩定性
    };

    esp_err_t err = i2c_new_master_bus(&buscfg, &s_i2c_bus);
    if (err != ESP_OK) return err;

    // 💡 自動掃描支援的傳感器地址列表：0x76(BME/BMP), 0x77(BME/BMP), 0x38(AHT系列)
    uint8_t probe_addrs[] = {0x76, 0x77, 0x38};
    for (size_t i = 0; i < sizeof(probe_addrs)/sizeof(probe_addrs[0]); i++) {
        if (i2c_master_probe(s_i2c_bus, probe_addrs[i], pdMS_TO_TICKS(100)) == ESP_OK) {
            g_bme280_data.i2c_addr = probe_addrs[i];
            break;
        }
    }

    if (g_bme280_data.i2c_addr == 0x00) {
        ESP_LOGE(TAG, "❌ 未在 I2C 總線上檢測到任何支持的溫濕度傳感器!");
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t devcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = g_bme280_data.i2c_addr,
        .scl_speed_hz = 100000, // 💡 降速到標準 100kHz！極大提高長杜邦線的抗干擾能力！
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &devcfg, &s_sensor_dev));

    // 分流初始化邏輯
    if (g_bme280_data.i2c_addr == 0x38) {
        g_bme280_data.sensor_type = SENSOR_TYPE_AHT20;
        return aht20_init_sensor();
    } else {
        // 讀取 Bosch 晶片 ID
        uint8_t chip_id = 0;
        if (i2c_read_bytes(0xD0, &chip_id, 1) != ESP_OK) return ESP_ERR_INVALID_RESPONSE;
        
        if (chip_id == 0x60) {
            g_bme280_data.sensor_type = SENSOR_TYPE_BME280;
            ESP_LOGI(TAG, "✅ 檢測到 BME280 (溫+濕+壓)");
        } else if (chip_id == 0x58) {
            g_bme280_data.sensor_type = SENSOR_TYPE_BMP280;
            ESP_LOGI(TAG, "✅ 檢測到 BMP280 (溫+壓, 注意: 本芯片物理無濕度功能)");
        } else {
            return ESP_ERR_NOT_SUPPORTED;
        }

        // 載入 BME/BMP 校準參數
        i2c_write_byte(0xE0, 0xB6); vTaskDelay(pdMS_TO_TICKS(15));
        uint8_t calib_buf[32];
        i2c_read_bytes(0x88, calib_buf, 24);
        
        s_calib.dig_T1 = (uint16_t)((calib_buf[1] << 8) | calib_buf[0]);
        s_calib.dig_T2 = (int16_t)((calib_buf[3] << 8) | calib_buf[2]);
        s_calib.dig_T3 = (int16_t)((calib_buf[5] << 8) | calib_buf[4]);
        s_calib.dig_P1 = (uint16_t)((calib_buf[7] << 8) | calib_buf[6]);
        s_calib.dig_P2 = (int16_t)((calib_buf[9] << 8) | calib_buf[8]);
        s_calib.dig_P3 = (int16_t)((calib_buf[11] << 8) | calib_buf[10]);
        s_calib.dig_P4 = (int16_t)((calib_buf[13] << 8) | calib_buf[12]);
        s_calib.dig_P5 = (int16_t)((calib_buf[15] << 8) | calib_buf[14]);
        s_calib.dig_P6 = (int16_t)((calib_buf[17] << 8) | calib_buf[16]);
        s_calib.dig_P7 = (int16_t)((calib_buf[19] << 8) | calib_buf[18]);
        s_calib.dig_P8 = (int16_t)((calib_buf[21] << 8) | calib_buf[20]);
        s_calib.dig_P9 = (int16_t)((calib_buf[23] << 8) | calib_buf[22]);

        if (g_bme280_data.i2c_addr == SENSOR_TYPE_BME280) {
            i2c_read_bytes(0xE1, calib_buf + 24, 7);
            s_calib.dig_H1 = calib_buf[24];
            s_calib.dig_H2 = (int16_t)((calib_buf[26] << 8) | calib_buf[25]);
            s_calib.dig_H3 = calib_buf[27];
            s_calib.dig_H4 = ((int16_t)((int8_t)calib_buf[28] << 4)) | (calib_buf[29] & 0x0F);
            s_calib.dig_H5 = ((int16_t)((int8_t)calib_buf[30] << 4)) | ((calib_buf[29] >> 4) & 0x0F);
            s_calib.dig_H6 = (int8_t)calib_buf[31];
            i2c_write_byte(0xF2, 0x01); // 設置濕度過採樣
        }

        i2c_write_byte(0xF5, 0x00);
        i2c_write_byte(0xF4, 0x27); // Normal Mode, 溫壓過採樣
        vTaskDelay(pdMS_TO_TICKS(20));
        return bme280_read_measurements();
    }
}

// ==========================================
// 統一讀取接口與終極防呆防彈濾波
// ==========================================
esp_err_t bme280_read_measurements(void) {
    if (!s_sensor_dev || g_bme280_data.sensor_type == SENSOR_TYPE_NONE) return ESP_ERR_INVALID_STATE;

    float raw_t = 0.0f, raw_h = -1.0f, raw_p = -1.0f;
    esp_err_t err = ESP_OK;

    if (g_bme280_data.sensor_type == SENSOR_TYPE_AHT20) {
        err = aht20_read_data(&raw_t, &raw_h);
        raw_p = -1.0f; // AHT 不支持氣壓
    } else {
        // BME280 或 BMP280 讀取
        i2c_write_byte(0xF4, 0x27);
        vTaskDelay(pdMS_TO_TICKS(50));

        uint8_t data[8];
        err = i2c_read_bytes(0xF7, data, (g_bme280_data.sensor_type == SENSOR_TYPE_BME280) ? 8 : 6);
        if (err != ESP_OK) return err;

        int32_t adc_p = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | ((data[2] >> 4) & 0x0F);
        int32_t adc_t = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | ((data[5] >> 4) & 0x0F);

        int32_t var1 = (((adc_t >> 3) - ((int32_t)s_calib.dig_T1 << 1)) * (int32_t)s_calib.dig_T2) >> 11;
        int32_t var2 = (((((adc_t >> 4) - (int32_t)s_calib.dig_T1) * ((adc_t >> 4) - (int32_t)s_calib.dig_T1)) >> 12) * (int32_t)s_calib.dig_T3) >> 14;
        int32_t t_fine = var1 + var2;
        raw_t = ((t_fine * 5 + 128) >> 8) / 100.0f;

        // 氣壓計算
        int64_t var1_64 = (int64_t)t_fine - 128000;
        int64_t var2_64 = var1_64 * var1_64 * (int64_t)s_calib.dig_P6;
        var2_64 = var2_64 + ((var1_64 * (int64_t)s_calib.dig_P5) << 17);
        var2_64 = var2_64 + ((int64_t)s_calib.dig_P4 << 35);
        var1_64 = ((var1_64 * var1_64 * (int64_t)s_calib.dig_P3) >> 8) + ((var1_64 * (int64_t)s_calib.dig_P2) << 12);
        var1_64 = (((((int64_t)1 << 47) + var1_64)) * (int64_t)s_calib.dig_P1) >> 33;
        if (var1_64 != 0) {
            int64_t p = 1048576 - (int64_t)adc_p;
            p = (((p << 31) - var2_64) * 3125) / var1_64;
            var1_64 = ((int64_t)s_calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
            var2_64 = ((int64_t)s_calib.dig_P8 * p) >> 19;
            p = ((p + var1_64 + var2_64) >> 8) + ((int64_t)s_calib.dig_P7 << 4);
            raw_p = (float)p / 25600.0f;
        }

        // BME280 專屬濕度計算
        if (g_bme280_data.sensor_type == SENSOR_TYPE_BME280) {
            int32_t adc_h = ((int32_t)data[6] << 8) | data[7];
            int32_t v_x1 = (t_fine - 76800);
            v_x1 = (((((adc_h << 14) - ((int32_t)s_calib.dig_H4 << 20) - ((int32_t)s_calib.dig_H5 * v_x1)) + 16384) >> 15) * (((((((v_x1 * (int32_t)s_calib.dig_H6) >> 10) * (((v_x1 * (int32_t)s_calib.dig_H3) >> 11) + 32768)) >> 10) + 2097152) * (int32_t)s_calib.dig_H2 + 8192) >> 14));
            v_x1 = v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * (int32_t)s_calib.dig_H1) >> 4);
            v_x1 = v_x1 < 0 ? 0 : (v_x1 > 419430400 ? 419430400 : v_x1);
            raw_h = (v_x1 >> 12) / 1024.0f;
        }
    }

    if (err != ESP_OK) return err;

    // 如果讀出的溫度超出地球人類生活極限，直接丟棄，保留上一禎合法值！
    if (raw_t > 60.0f || raw_t < -20.0f) {
        ESP_LOGW(TAG, "⚠️ 攔截到非法通訊異常溫度: %.2f°C，已丟棄!", raw_t);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // 只有校驗合法後，才真正賦值給對外輸出的全域變數
    g_bme280_data.temperature = raw_t;
    g_bme280_data.humidity_pct  = raw_h;
    g_bme280_data.pressure_hpa  = raw_p;

    return ESP_OK;
}