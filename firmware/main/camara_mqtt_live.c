/**
 * camara_mqtt_live - ESP32-CAM con stream MJPEG por HTTP y estado por MQTT.
 *
 * Base: camara_nodered_mqtt (se reutiliza inicializacion de camara/WiFi/MQTT).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_camera.h"
#include "sensor.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_random.h"

static const char *TAG = "camara_mqtt_live";

#define MQTT_TOPIC_ESTADO "camara/estado"
#define MQTT_TOPIC_IP     "camara/ip"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define PART_BOUNDARY "123456789000000000000987654321"
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" PART_BOUNDARY
#define STREAM_BOUNDARY "\r\n--" PART_BOUNDARY "\r\n"
#define STREAM_PART "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_mqtt_client_handle_t s_mqtt = NULL;
static bool s_camera_ok = false;
static char s_sta_ip_str[16];
static httpd_handle_t s_httpd = NULL;

#if CONFIG_IDF_TARGET_ESP32
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   (-1)
#define CAM_PIN_XCLK    0
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22
#else
#error "Target no soportado: usa ESP32"
#endif

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = 10000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 15,
    /* Evita overflow cuando no hay cliente consumiendo stream */
    .fb_count = 1,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
#if CONFIG_SPIRAM
    .fb_location = CAMERA_FB_IN_PSRAM,
#endif
};

static const char *INDEX_HTML =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<title>ESP32-CAM Stream</title>"
    "<style>body{font-family:Arial,sans-serif;background:#111;color:#eee;text-align:center}"
    "img{max-width:95vw;height:auto;border:1px solid #444;margin-top:10px}"
    "a{color:#7dcfff}</style></head><body>"
    "<h2>ESP32-CAM MJPEG</h2>"
    "<p>Stream: <a href='/stream'>/stream</a> | Snapshot: <a href='/snapshot'>/snapshot</a></p>"
    "<img src='/stream' alt='stream'/>"
    "</body></html>";

static const char *wifi_reason_str(uint8_t r)
{
    switch (r) {
    case 2: return "AUTH_EXPIRE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 202: return "AUTH_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    case 210: return "HANDSHAKE_FAIL/clave?";
    case 211: return "NO_AP_IN_AUTHMODE_THRESHOLD";
    default: return "?";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi desconectado, motivo=%u (%s)", ev->reason, wifi_reason_str(ev->reason));
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
            s_retry_num++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&ev->ip_info.ip, s_sta_ip_str, sizeof(s_sta_ip_str));
        s_retry_num = 0;
        esp_wifi_set_ps(WIFI_PS_NONE);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    s_retry_num = 0;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t w = {0};
#if CONFIG_WIFI_FORCE_WPA2
    w.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    w.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;
#else
    w.sta.threshold.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    w.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif
    w.sta.pmf_cfg.capable = true;
    w.sta.pmf_cfg.required = false;
    strncpy((char *)w.sta.ssid, CONFIG_ESP_WIFI_SSID, sizeof(w.sta.ssid) - 1);
    strncpy((char *)w.sta.password, CONFIG_ESP_WIFI_PASSWORD, sizeof(w.sta.password) - 1);

#if CONFIG_WIFI_USE_ALT_MAC
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0};
    esp_fill_random(&mac[1], 5);
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, mac));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &w));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi fallo. Revisa SSID/contrasena en menuconfig.");
        return false;
    }
    ESP_LOGI(TAG, "WiFi conectado. IP: %s", s_sta_ip_str);
    return true;
}

static esp_err_t camera_init(void)
{
    if (s_camera_ok) return ESP_OK;
    ESP_LOGI(TAG, "Iniciando camara...");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camara init: %s", esp_err_to_name(err));
        esp_camera_deinit();
        return err;
    }
    sensor_t *sens = esp_camera_sensor_get();
    if (sens) {
        sens->set_pixformat(sens, PIXFORMAT_JPEG);
        sens->set_framesize(sens, FRAMESIZE_VGA);
        sens->set_quality(sens, 15);
    }
    s_camera_ok = true;
    ESP_LOGI(TAG, "Camara lista (VGA JPEG q15).");
    return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t hlen = 0;
    char part_buf[64];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Frame capturado nulo");
            res = ESP_FAIL;
            break;
        }

        hlen = (size_t)snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)fb->len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);

        esp_camera_fb_return(fb);
        fb = NULL;

        if (res != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (fb) {
        esp_camera_fb_return(fb);
    }
    ESP_LOGI(TAG, "Cliente stream desconectado");
    return res;
}

static esp_err_t snapshot_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return ret;
}

static esp_err_t health_handler(httpd_req_t *req)
{
    const char *msg = "ok";
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, msg, strlen(msg));
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static httpd_handle_t start_camera_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 4;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handler,
            .user_ctx = NULL
        };
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_uri_t snap_uri = {
            .uri = "/snapshot",
            .method = HTTP_GET,
            .handler = snapshot_handler,
            .user_ctx = NULL
        };
        httpd_uri_t health_uri = {
            .uri = "/health",
            .method = HTTP_GET,
            .handler = health_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &stream_uri);
        httpd_register_uri_handler(server, &snap_uri);
        httpd_register_uri_handler(server, &health_uri);
        ESP_LOGI(TAG, "HTTP server listo en puerto %d", config.server_port);
    }
    return server;
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t ev = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado");
        esp_mqtt_client_publish(ev->client, MQTT_TOPIC_ESTADO, "online", 6, 0, 0);
        esp_mqtt_client_publish(ev->client, MQTT_TOPIC_IP, s_sta_ip_str, -1, 0, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT desconectado");
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    char uri[64];
    snprintf(uri, sizeof(uri), "mqtt://%s:1883", CONFIG_SERVER_IP);
    esp_mqtt_client_config_t mqtt_cfg = { .broker.address.uri = uri };
    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
    ESP_LOGI(TAG, "MQTT iniciado (broker %s)", uri);
}

void app_main(void)
{
    ESP_LOGI(TAG, "camara_mqtt_live - stream MJPEG + MQTT estado");

#if CONFIG_IDF_TARGET_ESP32
    gpio_config_t io32 = {
        .pin_bit_mask = (1ULL << 32),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io32);
    gpio_set_level(32, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    gpio_set_level(32, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
#endif

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!wifi_init_sta()) return;
    if (camera_init() != ESP_OK) return;

    s_httpd = start_camera_http_server();
    if (!s_httpd) {
        ESP_LOGE(TAG, "No se pudo iniciar servidor HTTP");
        return;
    }

    mqtt_start();
    ESP_LOGI(TAG, "Stream listo: http://%s/stream", s_sta_ip_str);
    ESP_LOGI(TAG, "Snapshot: http://%s/snapshot", s_sta_ip_str);
}
