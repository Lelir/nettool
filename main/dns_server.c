#include <string.h>
#include <errno.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "dns_server.h"

static const char *TAG = "dns_server";
#define DNS_PORT 53
#define DNS_BUF_SIZE 512

static int skip_dns_name(const uint8_t *buf, int len, int pos)
{
    while (pos < len) {
        uint8_t n = buf[pos++];
        if (n == 0) return pos;

        if ((n & 0xC0) == 0xC0) {
            if (pos < len) return pos + 1;
            return -1;
        }

        if (pos + n > len) return -1;
        pos += n;
    }
    return -1;
}

static int build_dns_reply(const uint8_t *req, int req_len,
                           uint8_t *resp, int resp_size)
{
    if (req_len < 12 || resp_size < req_len + 16) return -1;

    uint16_t qdcount = ((uint16_t)req[4] << 8) | req[5];
    if (qdcount == 0) return -1;

    int qname_end = skip_dns_name(req, req_len, 12);
    if (qname_end < 0 || qname_end + 4 > req_len) return -1;

    uint16_t qtype  = ((uint16_t)req[qname_end] << 8) | req[qname_end + 1];
    uint16_t qclass = ((uint16_t)req[qname_end + 2] << 8) | req[qname_end + 3];
    int question_end = qname_end + 4;

    memcpy(resp, req, question_end);

    resp[2] = 0x84 | (req[2] & 0x01);
    resp[3] = 0x00;
    resp[4] = 0x00; resp[5] = 0x01;

    bool answer_a = (qtype == 1 && qclass == 1);
    resp[6] = 0x00; resp[7] = answer_a ? 0x01 : 0x00;
    resp[8] = resp[9] = resp[10] = resp[11] = 0x00;

    if (!answer_a) return question_end;

    int p = question_end;
    resp[p++] = 0xC0; resp[p++] = 0x0C;
    resp[p++] = 0x00; resp[p++] = 0x01;
    resp[p++] = 0x00; resp[p++] = 0x01;
    resp[p++] = 0x00; resp[p++] = 0x00; resp[p++] = 0x00; resp[p++] = 0x1E;
    resp[p++] = 0x00; resp[p++] = 0x04;
    resp[p++] = 192;
    resp[p++] = 168;
    resp[p++] = 4;
    resp[p++] = 1;

    return p;
}

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind failed: errno=%d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Wildcard DNS ready on UDP/53 -> 192.168.4.1");

    uint8_t req[DNS_BUF_SIZE];
    uint8_t resp[DNS_BUF_SIZE];

    while (1) {
        struct sockaddr_in source = {0};
        socklen_t slen = sizeof(source);

        int len = recvfrom(sock, req, sizeof(req), 0,
                           (struct sockaddr *)&source, &slen);
        if (len <= 0) continue;

        int out_len = build_dns_reply(req, len, resp, sizeof(resp));
        if (out_len > 0) {
            sendto(sock, resp, out_len, 0,
                   (struct sockaddr *)&source, slen);
        }
    }
}

esp_err_t dns_server_start(void)
{
    BaseType_t ok = xTaskCreate(dns_task, "dns_server", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
