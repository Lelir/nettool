#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "discovery.h"
#include "nettool_state.h"

static const char *TAG = "discovery";

static const uint8_t LLDP_DST[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};
static const uint8_t CDP_DST[6]  = {0x01, 0x00, 0x0C, 0xCC, 0xCC, 0xCC};

static void copy_text(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
    if (!dst || dst_len == 0) {
        return;
    }

    size_t n = src_len;
    if (n >= dst_len) {
        n = dst_len - 1;
    }

    for (size_t i = 0; i < n; ++i) {
        uint8_t c = src[i];
        dst[i] = (c >= 32 && c <= 126) ? (char)c : ' ';
    }
    dst[n] = '\0';

    while (n > 0 && dst[n - 1] == ' ') {
        dst[--n] = '\0';
    }
}

static uint16_t be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           p[3];
}

static void format_ipv4(char *dst, size_t dst_len, const uint8_t *p)
{
    snprintf(dst, dst_len, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

void discovery_clear(void)
{
    nettool_state_t *s = nettool_state_get();

    s->switch_name[0] = '\0';
    s->switch_port[0] = '\0';
    s->switch_mgmt_ip[0] = '\0';
    s->switch_description[0] = '\0';
    s->switch_platform[0] = '\0';
    s->vlan[0] = '\0';
    s->discovery_source[0] = '\0';
    s->discovery_frames = 0;
    s->discovery_last_seen_us = 0;
}

static void parse_lldp(const uint8_t *payload, size_t len)
{
    nettool_state_t *s = nettool_state_get();

    strncpy(s->discovery_source, "LLDP", sizeof(s->discovery_source) - 1);
    s->discovery_source[sizeof(s->discovery_source) - 1] = '\0';
    s->discovery_frames++;
    s->discovery_last_seen_us = esp_timer_get_time();

    size_t pos = 0;
    while (pos + 2 <= len) {
        uint16_t h = be16(&payload[pos]);
        pos += 2;

        uint8_t type = (h >> 9) & 0x7F;
        uint16_t tlv_len = h & 0x01FF;

        if (pos + tlv_len > len) {
            break;
        }

        const uint8_t *v = &payload[pos];

        if (type == 0) {
            break;
        }

        /* Port ID: first byte is the LLDP subtype. */
        if (type == 2 && tlv_len > 1) {
            copy_text(s->switch_port, sizeof(s->switch_port), v + 1, tlv_len - 1);
        }

        /* Port Description can be useful when Port ID is numeric or a MAC. */
        if (type == 4 && tlv_len > 0 && s->switch_port[0] == '\0') {
            copy_text(s->switch_port, sizeof(s->switch_port), v, tlv_len);
        }

        if (type == 5 && tlv_len > 0) {
            copy_text(s->switch_name, sizeof(s->switch_name), v, tlv_len);
        }

        if (type == 6 && tlv_len > 0) {
            copy_text(s->switch_description, sizeof(s->switch_description), v, tlv_len);
        }

        /* Management Address TLV.
         * [addr-string-len][addr-subtype][addr][if-subtype][if-num][oid-len]... */
        if (type == 8 && tlv_len >= 7) {
            uint8_t addr_string_len = v[0];
            if (addr_string_len == 5 && tlv_len >= 6 && v[1] == 1) {
                format_ipv4(s->switch_mgmt_ip, sizeof(s->switch_mgmt_ip), &v[2]);
            }
        }

        /* IEEE 802.1 organizational TLV, subtype 1 = Port VLAN ID (PVID). */
        if (type == 127 && tlv_len >= 6 &&
            v[0] == 0x00 && v[1] == 0x80 && v[2] == 0xC2 && v[3] == 0x01) {
            uint16_t vlan = be16(&v[4]);
            snprintf(s->vlan, sizeof(s->vlan), "%u", vlan);
        }

        pos += tlv_len;
    }

    ESP_LOGI(TAG, "LLDP: switch=%s port=%s vlan=%s mgmt=%s",
             s->switch_name[0] ? s->switch_name : "-",
             s->switch_port[0] ? s->switch_port : "-",
             s->vlan[0] ? s->vlan : "-",
             s->switch_mgmt_ip[0] ? s->switch_mgmt_ip : "-");
}

static void parse_cdp_addresses(nettool_state_t *s, const uint8_t *v, size_t len)
{
    if (len < 4) {
        return;
    }

    uint32_t count = be32(v);
    size_t pos = 4;

    for (uint32_t i = 0; i < count && pos + 2 <= len; ++i) {
        uint8_t protocol_type = v[pos++];
        uint8_t protocol_len = v[pos++];

        if (pos + protocol_len + 2 > len) {
            return;
        }

        const uint8_t *protocol = &v[pos];
        pos += protocol_len;

        uint16_t addr_len = be16(&v[pos]);
        pos += 2;

        if (pos + addr_len > len) {
            return;
        }

        /* NLPID 0xCC is IPv4 in CDP. Some implementations also use
         * SNAP protocol encoding; a 4-byte address is enough for display. */
        if (addr_len == 4 &&
            ((protocol_type == 1 && protocol_len == 1 && protocol[0] == 0xCC) ||
             protocol_type == 2)) {
            format_ipv4(s->switch_mgmt_ip, sizeof(s->switch_mgmt_ip), &v[pos]);
            return;
        }

        pos += addr_len;
    }
}

static void parse_cdp(const uint8_t *payload, size_t len)
{
    /* LLC/SNAP (8 bytes) + CDP header (4 bytes). */
    if (len < 12 ||
        payload[0] != 0xAA || payload[1] != 0xAA || payload[2] != 0x03 ||
        payload[3] != 0x00 || payload[4] != 0x00 || payload[5] != 0x0C ||
        payload[6] != 0x20 || payload[7] != 0x00) {
        return;
    }

    nettool_state_t *s = nettool_state_get();
    int64_t now = esp_timer_get_time();

    /* Prefer LLDP when both protocols are enabled on a Cisco switch. CDP can
     * still fill useful Cisco-only fields such as platform/native VLAN. */
    bool recent_lldp = strcmp(s->discovery_source, "LLDP") == 0 &&
                       s->discovery_last_seen_us > 0 &&
                       (now - s->discovery_last_seen_us) < 180000000LL;

    char cdp_name[64] = {0};
    char cdp_port[64] = {0};
    char cdp_mgmt[48] = {0};
    char cdp_desc[128] = {0};
    char cdp_platform[64] = {0};
    char cdp_vlan[32] = {0};

    nettool_state_t tmp = {0};

    size_t pos = 12;
    while (pos + 4 <= len) {
        uint16_t type = be16(&payload[pos]);
        uint16_t tlv_len = be16(&payload[pos + 2]);

        if (tlv_len < 4 || pos + tlv_len > len) {
            break;
        }

        const uint8_t *v = &payload[pos + 4];
        size_t vlen = tlv_len - 4;

        switch (type) {
        case 0x0001: /* Device ID */
            copy_text(cdp_name, sizeof(cdp_name), v, vlen);
            break;
        case 0x0002: /* Addresses */
            parse_cdp_addresses(&tmp, v, vlen);
            strncpy(cdp_mgmt, tmp.switch_mgmt_ip, sizeof(cdp_mgmt) - 1);
            break;
        case 0x0003: /* Port ID */
            copy_text(cdp_port, sizeof(cdp_port), v, vlen);
            break;
        case 0x0005: /* Software Version */
            copy_text(cdp_desc, sizeof(cdp_desc), v, vlen);
            break;
        case 0x0006: /* Platform */
            copy_text(cdp_platform, sizeof(cdp_platform), v, vlen);
            break;
        case 0x000A: /* Native VLAN */
            if (vlen >= 2) {
                snprintf(cdp_vlan, sizeof(cdp_vlan), "%u", be16(v));
            }
            break;
        default:
            break;
        }

        pos += tlv_len;
    }

    s->discovery_frames++;

    if (!recent_lldp) {
        strncpy(s->discovery_source, "CDP", sizeof(s->discovery_source) - 1);
        s->discovery_source[sizeof(s->discovery_source) - 1] = '\0';
        s->discovery_last_seen_us = now;

        if (cdp_name[0]) strncpy(s->switch_name, cdp_name, sizeof(s->switch_name) - 1);
        if (cdp_port[0]) strncpy(s->switch_port, cdp_port, sizeof(s->switch_port) - 1);
        if (cdp_mgmt[0]) strncpy(s->switch_mgmt_ip, cdp_mgmt, sizeof(s->switch_mgmt_ip) - 1);
        if (cdp_desc[0]) strncpy(s->switch_description, cdp_desc, sizeof(s->switch_description) - 1);
    }

    if (cdp_platform[0]) {
        strncpy(s->switch_platform, cdp_platform, sizeof(s->switch_platform) - 1);
    }
    if (cdp_vlan[0] && s->vlan[0] == '\0') {
        strncpy(s->vlan, cdp_vlan, sizeof(s->vlan) - 1);
    }

    ESP_LOGI(TAG, "CDP: switch=%s port=%s vlan=%s platform=%s",
             cdp_name[0] ? cdp_name : "-",
             cdp_port[0] ? cdp_port : "-",
             cdp_vlan[0] ? cdp_vlan : "-",
             cdp_platform[0] ? cdp_platform : "-");
}

void discovery_process_frame(const uint8_t *frame, size_t len)
{
    if (!frame || len < 14) {
        return;
    }

    if (memcmp(frame, LLDP_DST, 6) == 0) {
        uint16_t ether_type = be16(&frame[12]);
        if (ether_type == 0x88CC) {
            parse_lldp(frame + 14, len - 14);
        }
        return;
    }

    if (memcmp(frame, CDP_DST, 6) == 0) {
        /* CDP uses an IEEE 802.3 length field followed by LLC/SNAP. */
        parse_cdp(frame + 14, len - 14);
    }
}
