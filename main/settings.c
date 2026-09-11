#include <string.h>

#include "nvs.h"
#include "esp_log.h"

#include "settings.h"

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "nettool";

void settings_defaults(nettool_settings_t *settings)
{
    if (!settings) {
        return;
    }

    memset(settings, 0, sizeof(*settings));
    settings->dhcp = true;
}

esp_err_t settings_load(nettool_settings_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_defaults(settings);

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t mode = 0;
    if (nvs_get_u8(nvs, "ip_mode", &mode) == ESP_OK) {
        settings->dhcp = (mode == 0);
    }

    uint32_t value = 0;
    if (nvs_get_u32(nvs, "static_ip", &value) == ESP_OK) {
        settings->static_ip.ip.addr = value;
    }
    if (nvs_get_u32(nvs, "static_mask", &value) == ESP_OK) {
        settings->static_ip.netmask.addr = value;
    }
    if (nvs_get_u32(nvs, "static_gw", &value) == ESP_OK) {
        settings->static_ip.gw.addr = value;
    }

    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t settings_save(const nettool_settings_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(nvs, "ip_mode", settings->dhcp ? 0 : 1);
    if (ret == ESP_OK) {
        ret = nvs_set_u32(nvs, "static_ip", settings->static_ip.ip.addr);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(nvs, "static_mask", settings->static_ip.netmask.addr);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u32(nvs, "static_gw", settings->static_ip.gw.addr);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }

    nvs_close(nvs);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Network settings saved (%s)", settings->dhcp ? "DHCP" : "static");
    }

    return ret;
}
