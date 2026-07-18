#ifndef OPENWRT_API_H
#define OPENWRT_API_H

#include <stdint.h>
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Structure to hold all real-time OpenWrt data
typedef struct {
    float download_speed;      // Mbps or MB/s calculated by ESP32
    float upload_speed;        // Mbps or MB/s calculated by ESP32
    int ping_ms;               // Real-time latency from router
    char today_used[32] ;      // Formatted string from router (e.g., "1.25 GB")
    char month_used[32];       // Formatted string from router (e.g., "45.80 GB")
    char ip4[16];              // IPv4 address (e.g., "192.168.100.1")
    char ip6[40];              // IPv6 address (e.g., "fe80::1ff:fe23:4567:890a")
    uint32_t last_update_time; // Timestamp of the last data update
} openwrt_data_t;

// Global variable to hold the latest OpenWrt data
extern openwrt_data_t g_openwrt_data;

/**
 * @brief Initialize and start the OpenWrt data fetch task.
 * This functions allocates memory in PSRAM and pins the task to Core 1.
 */
void openwrt_api_init(void);

#ifdef __cplusplus
}
#endif

#endif // OPENWRT_API_H