#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "dns_server.h"
#include "ethernet.h"
#include "nettool_state.h"
#include "webserver.h"
#include "wifi_ap.h"

static const char *TAG = "nettool";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // SoftAP + DHCP local.
    ESP_ERROR_CHECK(wifi_ap_start());

    // Wildcard DNS for captive portal.
    ESP_ERROR_CHECK(dns_server_start());

    // Ethernet is optional at boot. Its status remains visible over Wi-Fi.
    nettool_state_t *state = nettool_state_get();
    esp_err_t eth_ret = ethernet_start();
    state->eth_initialized = (eth_ret == ESP_OK);
    state->eth_init_error = eth_ret;

    if (eth_ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "Ethernet unavailable: %s. Continuing without Ethernet.",
                 esp_err_to_name(eth_ret));
    }

    ESP_ERROR_CHECK(webserver_start());
    ESP_LOGI(TAG, "NETTOOL V0.9 ready");
}
