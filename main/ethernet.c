#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"

#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"

#include "board_config.h"
#include "discovery.h"
#include "ethernet.h"
#include "nettool_state.h"
#include "settings.h"

static const char *TAG = "ethernet";

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static nettool_settings_t s_settings;

static bool dhcp_result_is_ok(esp_err_t ret)
{
    return ret == ESP_OK ||
           ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED ||
           ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED;
}

static void update_state_from_settings(void)
{
    nettool_state_t *s = nettool_state_get();
    s->ip_mode = s_settings.dhcp ? NETTOOL_IP_MODE_DHCP : NETTOOL_IP_MODE_STATIC;
    s->configured_static_ip = s_settings.static_ip;
}

static esp_err_t apply_static_settings(void)
{
    if (!s_eth_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_netif_dhcpc_stop(s_eth_netif);
    if (!dhcp_result_is_ok(ret) && ret != ESP_ERR_ESP_NETIF_IF_NOT_READY) {
        ESP_LOGE(TAG, "Could not stop DHCP client: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_netif_set_ip_info(s_eth_netif, &s_settings.static_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Could not set static IPv4 configuration: %s", esp_err_to_name(ret));
        return ret;
    }

    nettool_state_t *s = nettool_state_get();
    s->eth_ip = s_settings.static_ip;
    s->eth_has_ip = s->eth_link;

    ESP_LOGI(TAG, "Static IPv4 configured: " IPSTR " / " IPSTR " gw " IPSTR,
             IP2STR(&s_settings.static_ip.ip),
             IP2STR(&s_settings.static_ip.netmask),
             IP2STR(&s_settings.static_ip.gw));

    return ESP_OK;
}

static esp_err_t apply_dhcp_settings(void)
{
    if (!s_eth_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Clear an old static address before requesting a new lease. */
    esp_err_t ret = esp_netif_dhcpc_stop(s_eth_netif);
    if (!dhcp_result_is_ok(ret) && ret != ESP_ERR_ESP_NETIF_IF_NOT_READY) {
        return ret;
    }

    esp_netif_ip_info_t zero = {0};
    ret = esp_netif_set_ip_info(s_eth_netif, &zero);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_dhcpc_start(s_eth_netif);
    if (!dhcp_result_is_ok(ret)) {
        ESP_LOGE(TAG, "Could not start DHCP client: %s", esp_err_to_name(ret));
        return ret;
    }

    nettool_state_t *s = nettool_state_get();
    s->eth_has_ip = false;
    memset(&s->eth_ip, 0, sizeof(s->eth_ip));

    ESP_LOGI(TAG, "DHCP client enabled");
    return ESP_OK;
}

static esp_err_t ethernet_input_with_discovery(esp_eth_handle_t eth_handle,
                                                uint8_t *buffer,
                                                uint32_t length,
                                                void *priv)
{
    (void)eth_handle;

    if (!buffer || length < 14) {
        free(buffer);
        return ESP_OK;
    }

    /* W5500 must run in promiscuous/MACRAW mode to see non-IP link-local
     * multicast such as LLDP and CDP. Inspect every frame first. */
    discovery_process_frame(buffer, length);

    const uint8_t *dst = buffer;
    const uint8_t *our_mac = nettool_state_get()->eth_mac;
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    bool for_us = memcmp(dst, our_mac, 6) == 0;
    bool is_broadcast = memcmp(dst, broadcast, 6) == 0;
    bool is_multicast = (dst[0] & 0x01) != 0;

    /* Do not feed unrelated promiscuous unicast traffic into lwIP. */
    if (!for_us && !is_broadcast && !is_multicast) {
        free(buffer);
        return ESP_OK;
    }

    return esp_netif_receive((esp_netif_t *)priv, buffer, length, NULL);
}

static void enable_discovery_capture(void)
{
    /* LLDP (01:80:C2:...) and CDP (01:00:0C:...) are not IPv4 multicast.
     * The W5500 MAC filter does not reliably expose those frames through
     * the ordinary all-multicast setting, so use MACRAW/promiscuous mode
     * and filter unrelated unicast frames in software above. */
    bool enabled = true;
    esp_err_t ret = esp_eth_ioctl(s_eth_handle, ETH_CMD_S_PROMISCUOUS, &enabled);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Promiscuous L2 capture enabled for LLDP/CDP");
    } else {
        ESP_LOGE(TAG, "Could not enable LLDP/CDP capture: %s", esp_err_to_name(ret));
    }
}

static void eth_event_handler(void *arg,
                              esp_event_base_t base,
                              int32_t event_id,
                              void *event_data)
{
    (void)arg;
    (void)base;

    nettool_state_t *s = nettool_state_get();

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
        s->eth_link = true;

        esp_eth_handle_t h = *(esp_eth_handle_t *)event_data;
        eth_speed_t speed = ETH_SPEED_10M;
        eth_duplex_t duplex = ETH_DUPLEX_HALF;

        esp_eth_ioctl(h, ETH_CMD_G_SPEED, &speed);
        esp_eth_ioctl(h, ETH_CMD_G_DUPLEX_MODE, &duplex);
        esp_eth_ioctl(h, ETH_CMD_G_MAC_ADDR, s->eth_mac);

        s->eth_speed_mbps = (speed == ETH_SPEED_100M) ? 100 : 10;
        s->eth_full_duplex = (duplex == ETH_DUPLEX_FULL);

        ESP_LOGI(TAG, "Link UP, %d Mbps, %s duplex",
                 s->eth_speed_mbps,
                 s->eth_full_duplex ? "full" : "half");

        /* The default Ethernet netif starts DHCP when the link comes up.
         * If the selected mode is static we replace it immediately here. */
        if (!s_settings.dhcp) {
            esp_err_t ret = apply_static_settings();
            if (ret != ESP_OK) {
                snprintf(s->config_message, sizeof(s->config_message),
                         "Erreur IP statique: %s", esp_err_to_name(ret));
                s->config_message_error = true;
            }
        }
        break;
    }

    case ETHERNET_EVENT_DISCONNECTED:
        s->eth_link = false;
        s->eth_has_ip = false;
        memset(&s->eth_ip, 0, sizeof(s->eth_ip));
        discovery_clear();
        ESP_LOGW(TAG, "Link DOWN");
        break;

    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;

    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        break;

    default:
        break;
    }
}

static void got_ip_handler(void *arg,
                           esp_event_base_t base,
                           int32_t event_id,
                           void *event_data)
{
    (void)arg;
    (void)base;
    (void)event_id;

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    nettool_state_t *s = nettool_state_get();

    if (event->esp_netif != s_eth_netif) {
        return;
    }

    s->eth_ip = event->ip_info;
    s->eth_has_ip = true;

    ESP_LOGI(TAG, "%s IPv4 configuration acquired",
             s_settings.dhcp ? "DHCP" : "Static");
    ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "MASK: " IPSTR, IP2STR(&event->ip_info.netmask));
    ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&event->ip_info.gw));
}

esp_err_t ethernet_use_dhcp(void)
{
    s_settings.dhcp = true;

    esp_err_t ret = settings_save(&s_settings);
    if (ret != ESP_OK) {
        return ret;
    }

    update_state_from_settings();

    if (!s_eth_netif || !nettool_state_get()->eth_link) {
        return ESP_OK;
    }

    return apply_dhcp_settings();
}

esp_err_t ethernet_use_static_ip(const char *ip, const char *mask, const char *gw)
{
    if (!ip || !mask || !gw) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_ip_info_t info = {0};

    if (esp_netif_str_to_ip4(ip, &info.ip) != ESP_OK ||
        esp_netif_str_to_ip4(mask, &info.netmask) != ESP_OK ||
        esp_netif_str_to_ip4(gw, &info.gw) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    if (info.ip.addr == 0 || info.netmask.addr == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_settings.dhcp = false;
    s_settings.static_ip = info;

    esp_err_t ret = settings_save(&s_settings);
    if (ret != ESP_OK) {
        return ret;
    }

    update_state_from_settings();

    if (!s_eth_netif || !nettool_state_get()->eth_link) {
        return ESP_OK;
    }

    return apply_static_settings();
}

esp_err_t ethernet_start(void)
{
    ESP_LOGI(TAG, "Initializing Waveshare W5500 Ethernet");

    settings_defaults(&s_settings);
    esp_err_t ret = settings_load(&s_settings);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not load saved network settings: %s", esp_err_to_name(ret));
        settings_defaults(&s_settings);
    }
    update_state_from_settings();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = NETTOOL_ETH_MOSI_GPIO,
        .miso_io_num = NETTOOL_ETH_MISO_GPIO,
        .sclk_io_num = NETTOOL_ETH_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    ret = spi_bus_initialize(NETTOOL_ETH_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = NETTOOL_ETH_CS_GPIO,
        .queue_size = 20,
        .cs_ena_posttrans = 2,
    };

    eth_w5500_config_t w5500_config =
        ETH_W5500_DEFAULT_CONFIG(NETTOOL_ETH_SPI_HOST, &dev_cfg);
    w5500_config.base.int_gpio_num = NETTOOL_ETH_INT_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = 4096;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "Could not create W5500 MAC");
        return ESP_FAIL;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = NETTOOL_ETH_RST_GPIO;

    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (!phy) {
        ESP_LOGE(TAG, "Could not create W5500 PHY");
        mac->del(mac);
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);

    ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(ret));
        phy->del(phy);
        mac->del(mac);
        s_eth_handle = NULL;
        return ret;
    }

    /* Give the W5500 a deterministic unique MAC derived from the ESP32-S3.
     * The locally-administered bit avoids pretending that this is a vendor OUI. */
    uint8_t eth_mac_addr[6] = {0};
    ret = esp_read_mac(eth_mac_addr, ESP_MAC_WIFI_STA);
    if (ret == ESP_OK) {
        eth_mac_addr[0] |= 0x02;
        eth_mac_addr[0] &= 0xFE;
        ret = esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac_addr);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Could not configure Ethernet MAC: %s", esp_err_to_name(ret));
        return ret;
    }

    memcpy(nettool_state_get()->eth_mac, eth_mac_addr, sizeof(eth_mac_addr));
    ESP_LOGI(TAG, "Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             eth_mac_addr[0], eth_mac_addr[1], eth_mac_addr[2],
             eth_mac_addr[3], eth_mac_addr[4], eth_mac_addr[5]);

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (!s_eth_netif) {
        ESP_LOGE(TAG, "Could not create Ethernet netif");
        return ESP_FAIL;
    }

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_eth_netif, glue),
                        TAG, "attach Ethernet netif");

    /* The standard glue owns the input path after attach. Replace it with a
     * tiny tee: inspect LLDP/CDP, then pass the exact same frame to lwIP. */
    ESP_RETURN_ON_ERROR(
        esp_eth_update_input_path(s_eth_handle,
                                  ethernet_input_with_discovery,
                                  s_eth_netif),
        TAG,
        "install discovery input path");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                   &eth_event_handler, NULL),
        TAG, "register ETH handler");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                   &got_ip_handler, NULL),
        TAG, "register IP handler");

    ESP_RETURN_ON_ERROR(esp_eth_start(s_eth_handle), TAG, "start Ethernet");

    enable_discovery_capture();

    ESP_LOGI(TAG, "W5500 Ethernet initialized (%s mode)",
             s_settings.dhcp ? "DHCP" : "static");

    return ESP_OK;
}

esp_eth_handle_t ethernet_get_handle(void)
{
    return s_eth_handle;
}

esp_netif_t *ethernet_get_netif(void)
{
    return s_eth_netif;
}
