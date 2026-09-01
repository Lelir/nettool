#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"

#include "board_config.h"
#include "ethernet.h"
#include "nettool_state.h"

static const char *TAG = "ethernet";

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;


/* ============================================================
 * Ethernet events
 * ============================================================ */

static void eth_event_handler(void *arg,
                              esp_event_base_t base,
                              int32_t event_id,
                              void *event_data)
{
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

        s->eth_speed_mbps =
            (speed == ETH_SPEED_100M) ? 100 : 10;

        s->eth_full_duplex =
            (duplex == ETH_DUPLEX_FULL);

        ESP_LOGI(TAG,
                 "Link UP, %d Mbps, %s duplex",
                 s->eth_speed_mbps,
                 s->eth_full_duplex ? "full" : "half");

        break;
    }


    case ETHERNET_EVENT_DISCONNECTED:

        s->eth_link = false;
        s->eth_has_ip = false;

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


/* ============================================================
 * DHCP event
 * ============================================================ */

static void got_ip_handler(void *arg,
                           esp_event_base_t base,
                           int32_t event_id,
                           void *event_data)
{
    ip_event_got_ip_t *event =
        (ip_event_got_ip_t *)event_data;

    nettool_state_t *s =
        nettool_state_get();

    s->eth_ip = event->ip_info;
    s->eth_has_ip = true;

    ESP_LOGI(TAG, "DHCP lease acquired");

    ESP_LOGI(TAG,
             "IP: " IPSTR,
             IP2STR(&event->ip_info.ip));

    ESP_LOGI(TAG,
             "MASK: " IPSTR,
             IP2STR(&event->ip_info.netmask));

    ESP_LOGI(TAG,
             "GW: " IPSTR,
             IP2STR(&event->ip_info.gw));
}


/* ============================================================
 * Start Ethernet
 *
 * Waveshare ESP32-S3-ETH
 * W5500 connected by SPI
 *
 * GPIO11 = MOSI
 * GPIO12 = MISO
 * GPIO13 = SCLK
 * GPIO14 = CS
 * GPIO9  = RESET
 * GPIO10 = INT
 * ============================================================ */

esp_err_t ethernet_start(void)
{
    ESP_LOGI(TAG, "Initializing Waveshare W5500 Ethernet");


    /* --------------------------------------------------------
     * SPI BUS
     * -------------------------------------------------------- */

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = NETTOOL_ETH_MOSI_GPIO,
        .miso_io_num = NETTOOL_ETH_MISO_GPIO,
        .sclk_io_num = NETTOOL_ETH_SCLK_GPIO,

        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        .max_transfer_sz = 4096,
    };


    esp_err_t ret =
        spi_bus_initialize(
            NETTOOL_ETH_SPI_HOST,
            &bus_cfg,
            SPI_DMA_CH_AUTO);

    if (ret != ESP_OK &&
        ret != ESP_ERR_INVALID_STATE) {

        ESP_LOGE(TAG,
                 "SPI bus init failed: %s",
                 esp_err_to_name(ret));

        return ret;
    }


    /* --------------------------------------------------------
     * W5500 SPI DEVICE
     * -------------------------------------------------------- */

    spi_device_interface_config_t dev_cfg = {

        .mode = 0,

        /*
         * 20 MHz is conservative and reliable.
         * W5500 can operate faster, but this is plenty
         * for NetTool.
         */
        .clock_speed_hz = 20 * 1000 * 1000,

        .spics_io_num = NETTOOL_ETH_CS_GPIO,

        .queue_size = 20,

        /*
         * Allows CS to remain asserted slightly longer
         * after the SPI transaction.
         */
        .cs_ena_posttrans = 2,
    };


    /* --------------------------------------------------------
     * W5500 configuration
     * -------------------------------------------------------- */

    eth_w5500_config_t w5500_config =
        ETH_W5500_DEFAULT_CONFIG(
            NETTOOL_ETH_SPI_HOST,
            &dev_cfg);


    w5500_config.base.int_gpio_num =
        NETTOOL_ETH_INT_GPIO;


    /* --------------------------------------------------------
     * MAC configuration
     * -------------------------------------------------------- */

    eth_mac_config_t mac_config =
        ETH_MAC_DEFAULT_CONFIG();

    /*
     * W5500 RX task may need more stack than default,
     * especially once NetTool grows.
     */
    mac_config.rx_task_stack_size = 4096;


    esp_eth_mac_t *mac =
        esp_eth_mac_new_w5500(
            &w5500_config,
            &mac_config);

    if (!mac) {

        ESP_LOGE(TAG,
                 "Could not create W5500 MAC");

        return ESP_FAIL;
    }


    /* --------------------------------------------------------
     * PHY configuration
     * -------------------------------------------------------- */

    eth_phy_config_t phy_config =
        ETH_PHY_DEFAULT_CONFIG();


    phy_config.reset_gpio_num =
        NETTOOL_ETH_RST_GPIO;


    esp_eth_phy_t *phy =
        esp_eth_phy_new_w5500(
            &phy_config);


    if (!phy) {

        ESP_LOGE(TAG,
                 "Could not create W5500 PHY");

        mac->del(mac);

        return ESP_FAIL;
    }


    /* --------------------------------------------------------
     * Ethernet driver
     * -------------------------------------------------------- */

    esp_eth_config_t eth_config =
        ETH_DEFAULT_CONFIG(
            mac,
            phy);


    ret =
        esp_eth_driver_install(
            &eth_config,
            &s_eth_handle);


    if (ret != ESP_OK) {

        ESP_LOGE(TAG,
                 "Ethernet driver install failed: %s",
                 esp_err_to_name(ret));

        phy->del(phy);
        mac->del(mac);

        s_eth_handle = NULL;

        return ret;
    }


    /* --------------------------------------------------------
     * ESP-NETIF
     * -------------------------------------------------------- */

    esp_netif_config_t netif_cfg =
        ESP_NETIF_DEFAULT_ETH();


    s_eth_netif =
        esp_netif_new(
            &netif_cfg);


    if (!s_eth_netif) {

        ESP_LOGE(TAG,
                 "Could not create Ethernet netif");

        return ESP_FAIL;
    }


    esp_eth_netif_glue_handle_t glue =
        esp_eth_new_netif_glue(
            s_eth_handle);


    ESP_RETURN_ON_ERROR(
        esp_netif_attach(
            s_eth_netif,
            glue),

        TAG,
        "attach Ethernet netif");


    /* --------------------------------------------------------
     * Events
     * -------------------------------------------------------- */

    ESP_RETURN_ON_ERROR(

        esp_event_handler_register(
            ETH_EVENT,
            ESP_EVENT_ANY_ID,
            &eth_event_handler,
            NULL),

        TAG,
        "register ETH handler");


    ESP_RETURN_ON_ERROR(

        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_ETH_GOT_IP,
            &got_ip_handler,
            NULL),

        TAG,
        "register IP handler");


    /* --------------------------------------------------------
     * Start
     * -------------------------------------------------------- */

    ESP_LOGI(TAG,
             "Starting W5500 Ethernet");


    ESP_RETURN_ON_ERROR(

        esp_eth_start(
            s_eth_handle),

        TAG,
        "start Ethernet");


    ESP_LOGI(TAG,
             "W5500 Ethernet initialized");


    return ESP_OK;
}


/* ============================================================
 * Accessors
 * ============================================================ */

esp_eth_handle_t ethernet_get_handle(void)
{
    return s_eth_handle;
}


esp_netif_t *ethernet_get_netif(void)
{
    return s_eth_netif;
}