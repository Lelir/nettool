#pragma once

#include "driver/spi_master.h"

/* =========================
 * Wi-Fi AP
 * ========================= */

#define NETTOOL_WIFI_SSID       "NetTool"
#define NETTOOL_WIFI_PASS       "nettool123"
#define NETTOOL_WIFI_MAX_STA    4

/* =========================
 * Waveshare ESP32-S3-ETH
 * W5500 Ethernet
 * ========================= */

#define NETTOOL_ETH_SPI_HOST    SPI2_HOST

#define NETTOOL_ETH_MOSI_GPIO   11
#define NETTOOL_ETH_MISO_GPIO   12
#define NETTOOL_ETH_SCLK_GPIO   13

#define NETTOOL_ETH_CS_GPIO     14
#define NETTOOL_ETH_RST_GPIO    9
#define NETTOOL_ETH_INT_GPIO    10