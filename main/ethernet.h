#pragma once

#include "esp_err.h"
#include "esp_eth.h"
#include "esp_netif.h"

esp_err_t ethernet_start(void);
esp_eth_handle_t ethernet_get_handle(void);
esp_netif_t *ethernet_get_netif(void);

esp_err_t ethernet_use_dhcp(void);
esp_err_t ethernet_use_static_ip(const char *ip, const char *mask, const char *gw);
