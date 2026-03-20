idf_component_register(
    SRCS "camara_mqtt_live.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp32-camera esp_wifi esp_netif esp_event nvs_flash mqtt esp_http_server driver
)
