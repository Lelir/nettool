#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

typedef struct {
    bool eth_initialized;
    esp_err_t eth_init_error;

    bool eth_link;
    bool eth_has_ip;
    int eth_speed_mbps;
    bool eth_full_duplex;
    uint8_t eth_mac[6];
    esp_netif_ip_info_t eth_ip;

    char switch_name[64];
    char switch_port[64];
    char vlan[32];
    char discovery_source[16];
} nettool_state_t;

nettool_state_t *nettool_state_get(void);
