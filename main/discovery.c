#include <string.h>
#include "esp_log.h"
#include "discovery.h"
#include "nettool_state.h"

static const char *TAG = "discovery";

static const uint8_t LLDP_DST[6] = {0x01,0x80,0xC2,0x00,0x00,0x0E};
static const uint8_t CDP_DST[6]  = {0x01,0x00,0x0C,0xCC,0xCC,0xCC};

static void parse_lldp(const uint8_t *payload, size_t len)
{
    nettool_state_t *s = nettool_state_get();
    strncpy(s->discovery_source, "LLDP", sizeof(s->discovery_source)-1);

    size_t pos = 0;
    while (pos + 2 <= len) {
        uint16_t h = ((uint16_t)payload[pos] << 8) | payload[pos+1];
        pos += 2;
        uint8_t type = (h >> 9) & 0x7F;
        uint16_t tlv_len = h & 0x01FF;
        if (pos + tlv_len > len) break;

        const uint8_t *v = &payload[pos];

        if (type == 0) break;

        // Port ID TLV
        if (type == 2 && tlv_len > 1) {
            size_t n = tlv_len - 1;
            if (n >= sizeof(s->switch_port)) n = sizeof(s->switch_port)-1;
            memcpy(s->switch_port, v + 1, n);
            s->switch_port[n] = 0;
        }

        // System Name TLV
        if (type == 5 && tlv_len > 0) {
            size_t n = tlv_len;
            if (n >= sizeof(s->switch_name)) n = sizeof(s->switch_name)-1;
            memcpy(s->switch_name, v, n);
            s->switch_name[n] = 0;
        }

        pos += tlv_len;
    }
}

static void parse_cdp(const uint8_t *payload, size_t len)
{
    // CDP decoding will be expanded in V1.
    nettool_state_t *s = nettool_state_get();
    strncpy(s->discovery_source, "CDP", sizeof(s->discovery_source)-1);
    ESP_LOGI(TAG, "CDP frame received (%u bytes)", (unsigned)len);
}

void discovery_process_frame(const uint8_t *frame, size_t len)
{
    if (!frame || len < 14) return;

    if (memcmp(frame, LLDP_DST, 6) == 0) {
        uint16_t ether_type = ((uint16_t)frame[12] << 8) | frame[13];
        if (ether_type == 0x88CC) {
            parse_lldp(frame + 14, len - 14);
        }
        return;
    }

    if (memcmp(frame, CDP_DST, 6) == 0) {
        // CDP uses 802.3 + LLC/SNAP rather than EtherType 0x88cc.
        parse_cdp(frame + 14, len - 14);
    }
}
