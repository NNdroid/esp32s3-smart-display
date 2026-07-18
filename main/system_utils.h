#pragma once
#include <esp_err.h>
#include <stddef.h>

static inline int min_int(int a, int b) {
    return (a < b) ? a : b;
}
esp_err_t nvs_save_u8(const char *key, uint8_t value);
esp_err_t nvs_load_u8(const char *key, uint8_t *out_value);
esp_err_t nvs_save_str(const char *key, const char *value);
esp_err_t nvs_load_str(const char *key, char *out_value, size_t max_len);
void load_current_face_from_nvs(void);
void save_current_face_to_nvs(void);
void wifi_init_basics(void);
void wait_for_wifi_connection(void);
void refresh_wifi_status(void);
void sync_time_sntp(void);