#ifndef MQTT_API_H
#define MQTT_API_H

#include <stdint.h>
#include <stdbool.h>

void mqtt_api_init(void);
void mqtt_api_reconnect(void);

// Buffer and state to be used by graphics
extern uint16_t *g_mqtt_image_buf;
extern bool g_mqtt_image_too_large;
extern bool g_mqtt_image_ready;
extern bool g_mqtt_connected;

#endif // MQTT_API_H
