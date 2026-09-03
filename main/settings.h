#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"

typedef struct {
    bool dhcp;
    esp_netif_ip_info_t static_ip;
} nettool_settings_t;

void settings_defaults(nettool_settings_t *settings);
esp_err_t settings_load(nettool_settings_t *settings);
esp_err_t settings_save(const nettool_settings_t *settings);
