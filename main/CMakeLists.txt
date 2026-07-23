idf_component_register(SRCS "baterry.c" "syslog_redirect.c" "font_cn_16x16.c" "app_actions.c" "openwrt_api.c" "buzzer.c" "system_utils.c" "graphics.c" "env_sensor_drv.c" "stock_api.c" "web_server.c" "ble_prov.c" "mqtt_api.c" "main.c"
                       PRIV_REQUIRES spi_flash esp_lcd_st7789
                       INCLUDE_DIRS "."
                       REQUIRES esp_adc esp_wifi esp_netif esp_event nvs_flash esp_http_client esp-tls cjson esp_http_server esp_driver_touch_sens mbedtls app_update esp_https_ota network_provisioning bt esp_driver_ledc esp_driver_spi mqtt
                       EMBED_TXTFILES "html/index.html" "html/css/style.css")
