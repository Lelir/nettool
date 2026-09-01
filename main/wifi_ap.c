#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "board_config.h"
#include "wifi_ap.h"

static const char *TAG = "wifi_ap";

esp_err_t wifi_ap_start(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read AP MAC");

    char ssid[33];
    snprintf(ssid, sizeof(ssid), "NETTOOL-%02X%02X", mac[4], mac[5]);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    strncpy((char *)wifi_config.ap.password, NETTOOL_WIFI_PASS,
            sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = NETTOOL_WIFI_MAX_STA;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");

    ESP_LOGI(TAG, "SoftAP ready: SSID=%s password=%s", ssid, NETTOOL_WIFI_PASS);
    ESP_LOGI(TAG, "Open http://192.168.4.1/");
    return ESP_OK;
}
