#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

typedef enum {
    NETTOOL_IP_MODE_DHCP = 0,
    NETTOOL_IP_MODE_STATIC = 1,
} nettool_ip_mode_t;

typedef struct {
    bool eth_initialized;
    esp_err_t eth_init_error;

    bool eth_link;
    bool eth_has_ip;
    int eth_speed_mbps;
    bool eth_full_duplex;
    uint8_t eth_mac[6];
    esp_netif_ip_info_t eth_ip;

    nettool_ip_mode_t ip_mode;
    esp_netif_ip_info_t configured_static_ip;
    char config_message[96];
    bool config_message_error;

    char switch_name[64];
    char switch_port[64];
    char switch_mgmt_ip[48];
    char switch_description[128];
    char switch_platform[64];
    char vlan[32];
    char discovery_source[16];
    uint32_t discovery_frames;
    int64_t discovery_last_seen_us;
} nettool_state_t;

nettool_state_t *nettool_state_get(void);
