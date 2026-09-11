#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"

#include "ethernet.h"
#include "nettool_state.h"
#include "webserver.h"

static const char *TAG = "webserver";

static esp_err_t send_chunk(httpd_req_t *req, const char *text)
{
    return httpd_resp_send_chunk(req, text, HTTPD_RESP_USE_STRLEN);
}

static void format_ip(char out[16], const esp_ip4_addr_t *addr, bool blank_if_zero)
{
    if (blank_if_zero && addr->addr == 0) {
        out[0] = '\0';
        return;
    }
    snprintf(out, 16, IPSTR, IP2STR(addr));
}

static void html_escape(const char *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) {
        return;
    }

    size_t w = 0;
    for (size_t i = 0; src && src[i] && w + 1 < dst_len; ++i) {
        const char *rep = NULL;
        switch (src[i]) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '\"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        default:
            dst[w++] = src[i];
            continue;
        }

        size_t n = strlen(rep);
        if (w + n >= dst_len) {
            break;
        }
        memcpy(&dst[w], rep, n);
        w += n;
    }
    dst[w] = '\0';
}

static esp_err_t root_get(httpd_req_t *req)
{
    nettool_state_t *s = nettool_state_get();

    char ip[16] = "-";
    char mask[16] = "-";
    char gw[16] = "-";
    if (s->eth_has_ip) {
        format_ip(ip, &s->eth_ip.ip, false);
        format_ip(mask, &s->eth_ip.netmask, false);
        format_ip(gw, &s->eth_ip.gw, false);
    }

    char cfg_ip[16] = "";
    char cfg_mask[16] = "";
    char cfg_gw[16] = "";
    format_ip(cfg_ip, &s->configured_static_ip.ip, true);
    format_ip(cfg_mask, &s->configured_static_ip.netmask, true);
    format_ip(cfg_gw, &s->configured_static_ip.gw, true);

    char sw_name[160], sw_port[160], sw_mgmt[96], sw_desc[300], sw_platform[160], vlan[80];
    html_escape(s->switch_name, sw_name, sizeof(sw_name));
    html_escape(s->switch_port, sw_port, sizeof(sw_port));
    html_escape(s->switch_mgmt_ip, sw_mgmt, sizeof(sw_mgmt));
    html_escape(s->switch_description, sw_desc, sizeof(sw_desc));
    html_escape(s->switch_platform, sw_platform, sizeof(sw_platform));
    html_escape(s->vlan, vlan, sizeof(vlan));

    char line[1024];

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    ESP_RETURN_ON_ERROR(send_chunk(req,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NETTOOL</title><style>"
        "*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;margin:0;background:#111;color:#eee}"
        ".w{max-width:820px;margin:auto;padding:18px}h1{margin:4px 0 18px}.c{background:#1d1d1d;border-radius:14px;padding:18px;margin:12px 0}"
        ".ok{color:#67e480;font-weight:650}.bad{color:#ff7b72;font-weight:650}.warn{color:#f2c94c;font-weight:650}"
        "table{width:100%;border-collapse:collapse}td{padding:8px;border-bottom:1px solid #333;vertical-align:top}td:first-child{color:#aaa;width:38%}"
        ".hint{color:#aaa;font-size:.92rem;line-height:1.45}.msg{padding:10px 12px;border-radius:9px;background:#252525;margin:10px 0}"
        "label{display:block;margin:10px 0 5px;color:#bbb}input[type=text]{width:100%;padding:10px;border:1px solid #444;border-radius:8px;background:#111;color:#eee;font-size:1rem}"
        ".modes{display:flex;gap:20px;margin:8px 0 14px}.actions{display:flex;gap:10px;margin-top:16px;flex-wrap:wrap}"
        "button,.btn{border:0;border-radius:9px;padding:10px 15px;background:#eee;color:#111;font-weight:650;text-decoration:none;cursor:pointer}"
        ".secondary{background:#333;color:#eee}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}"
        "@media(max-width:600px){.grid{grid-template-columns:1fr}td:first-child{width:44%}}"
        "</style></head><body><div class='w'><h1>NETTOOL V0.9</h1>"),
        TAG, "send header");

    if (s->config_message[0]) {
        char msg[220];
        html_escape(s->config_message, msg, sizeof(msg));
        snprintf(line, sizeof(line), "<div class='msg %s'>%s</div>",
                 s->config_message_error ? "bad" : "ok", msg);
        ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send message");
    }

    ESP_RETURN_ON_ERROR(send_chunk(req,
        "<div class='c'><h2>Wi-Fi</h2><table>"
        "<tr><td>Portail captif</td><td class='ok'>ACTIF</td></tr>"
        "<tr><td>Adresse</td><td>192.168.4.1</td></tr>"
        "</table></div>"), TAG, "send wifi");

    ESP_RETURN_ON_ERROR(send_chunk(req, "<div class='c'><h2>Ethernet</h2><table>"),
                        TAG, "send ethernet header");

    if (!s->eth_initialized) {
        snprintf(line, sizeof(line),
                 "<tr><td>Module Ethernet</td><td class='bad'>NON INITIALISÉ</td></tr>"
                 "<tr><td>Erreur</td><td>%s</td></tr>"
                 "<tr><td>Mode IP</td><td>%s</td></tr></table>"
                 "<p class='hint'>Le NetTool reste accessible en Wi-Fi.</p></div>",
                 esp_err_to_name(s->eth_init_error),
                 s->ip_mode == NETTOOL_IP_MODE_DHCP ? "DHCP" : "STATIQUE");
        ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send ethernet missing");
    } else {
        snprintf(line, sizeof(line),
                 "<tr><td>Module Ethernet</td><td class='ok'>INITIALISÉ</td></tr>"
                 "<tr><td>Link</td><td class='%s'>%s</td></tr>"
                 "<tr><td>Vitesse</td><td>%d Mbps %s</td></tr>"
                 "<tr><td>MAC</td><td>%02X:%02X:%02X:%02X:%02X:%02X</td></tr>"
                 "<tr><td>Mode IP</td><td>%s</td></tr>"
                 "<tr><td>IP</td><td>%s</td></tr>"
                 "<tr><td>Masque</td><td>%s</td></tr>"
                 "<tr><td>Gateway</td><td>%s</td></tr></table>",
                 s->eth_link ? "ok" : "warn", s->eth_link ? "UP" : "DOWN",
                 s->eth_speed_mbps, s->eth_full_duplex ? "FULL" : "HALF",
                 s->eth_mac[0], s->eth_mac[1], s->eth_mac[2], s->eth_mac[3], s->eth_mac[4], s->eth_mac[5],
                 s->ip_mode == NETTOOL_IP_MODE_DHCP ? "DHCP" : "STATIQUE",
                 ip, mask, gw);
        ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send ethernet state");

        if (!s->eth_link) {
            ESP_RETURN_ON_ERROR(send_chunk(req,
                "<p class='hint'>Aucun lien réseau détecté. La configuration IP peut quand même être préparée ci-dessous.</p>"),
                TAG, "send link hint");
        }
        ESP_RETURN_ON_ERROR(send_chunk(req, "</div>"), TAG, "close ethernet");
    }

    ESP_RETURN_ON_ERROR(send_chunk(req, "<div class='c'><h2>Découverte du switch</h2><table>"),
                        TAG, "send discovery header");

    uint64_t age_s = 0;
    if (s->discovery_last_seen_us > 0) {
        int64_t age_us = esp_timer_get_time() - s->discovery_last_seen_us;
        if (age_us > 0) {
            age_s = (uint64_t)(age_us / 1000000LL);
        }
    }

    char age[48] = "-";
    if (s->discovery_last_seen_us > 0) {
        snprintf(age, sizeof(age), "il y a %llu s", (unsigned long long)age_s);
    }

    /* Send discovery rows in small chunks. Some LLDP/CDP fields (especially
     * System Description) can be long, so building the whole table in one
     * snprintf() can legitimately exceed the temporary buffer. */
    snprintf(line, sizeof(line),
             "<tr><td>Protocole</td><td>%s</td></tr>"
             "<tr><td>Switch</td><td>%s</td></tr>"
             "<tr><td>Port</td><td>%s</td></tr>"
             "<tr><td>VLAN / PVID</td><td>%s</td></tr>",
             s->discovery_source[0] ? s->discovery_source : "En attente...",
             sw_name[0] ? sw_name : "-",
             sw_port[0] ? sw_port : "-",
             vlan[0] ? vlan : "-");
    ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send discovery state 1");

    snprintf(line, sizeof(line),
             "<tr><td>IP management</td><td>%s</td></tr>"
             "<tr><td>Plateforme</td><td>%s</td></tr>",
             sw_mgmt[0] ? sw_mgmt : "-",
             sw_platform[0] ? sw_platform : "-");
    ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send discovery state 2");

    snprintf(line, sizeof(line),
             "<tr><td>Description</td><td>%s</td></tr>",
             sw_desc[0] ? sw_desc : "-");
    ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send discovery description");

    snprintf(line, sizeof(line),
             "<tr><td>Dernière annonce</td><td>%s</td></tr></table>",
             age);
    ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send discovery state 3");

    ESP_RETURN_ON_ERROR(send_chunk(req,
        "<p class='hint'>Le NetTool écoute passivement LLDP et CDP. Selon la configuration du switch, une annonce peut prendre plusieurs dizaines de secondes.</p>"
        "</div>"), TAG, "send discovery hint");

    ESP_RETURN_ON_ERROR(send_chunk(req, "<div class='c'><h2>Configuration IPv4 Ethernet</h2>"),
                        TAG, "send config header");

    snprintf(line, sizeof(line),
             "<form method='post' action='/config' id='ipform'>"
             "<div class='modes'>"
             "<label><input type='radio' name='mode' value='dhcp' id='dhcp' %s> DHCP</label>"
             "<label><input type='radio' name='mode' value='static' id='static' %s> Statique</label>"
             "</div><div id='staticFields'><div class='grid'>",
             s->ip_mode == NETTOOL_IP_MODE_DHCP ? "checked" : "",
             s->ip_mode == NETTOOL_IP_MODE_STATIC ? "checked" : "");
    ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send config form 1");

    snprintf(line, sizeof(line),
             "<div><label for='ip'>Adresse IP</label>"
             "<input type='text' id='ip' name='ip' value='%s' placeholder='10.27.170.50'></div>"
             "<div><label for='mask'>Masque</label>"
             "<input type='text' id='mask' name='mask' value='%s' placeholder='255.255.254.0'></div>"
             "</div><label for='gw'>Gateway</label>"
             "<input type='text' id='gw' name='gw' value='%s' placeholder='10.27.170.1'>",
             cfg_ip, cfg_mask, cfg_gw);
    ESP_RETURN_ON_ERROR(send_chunk(req, line), TAG, "send config form 2");

    ESP_RETURN_ON_ERROR(send_chunk(req,
             "</div><div class='actions'><button type='submit'>Appliquer</button>"
             "<a class='btn secondary' href='/'>Actualiser</a></div></form>"),
             TAG, "send config form 3");

    ESP_RETURN_ON_ERROR(send_chunk(req,
        "<p class='hint'>Le choix DHCP/statique et les valeurs IPv4 sont enregistrés en NVS et conservés après redémarrage.</p>"
        "</div>"
        "<script>"
        "function mode(){var st=document.getElementById('static').checked;"
        "document.querySelectorAll('#staticFields input').forEach(function(e){e.disabled=!st;});}"
        "document.getElementById('dhcp').addEventListener('change',mode);"
        "document.getElementById('static').addEventListener('change',mode);mode();"
        "</script>"
        "</div></body></html>"), TAG, "send footer");

    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t config_post(httpd_req_t *req)
{
    nettool_state_t *s = nettool_state_get();

    if (req->content_len <= 0 || req->content_len >= 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
        return ESP_FAIL;
    }

    char body[256];
    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int ret = httpd_req_recv(req, body + received,
                                 (size_t)req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    body[received] = '\0';

    char mode[16] = {0};
    if (httpd_query_key_value(body, "mode", mode, sizeof(mode)) != ESP_OK) {
        snprintf(s->config_message, sizeof(s->config_message), "Mode IP manquant");
        s->config_message_error = true;
    } else {
        esp_err_t ret;

        if (strcmp(mode, "dhcp") == 0) {
            ret = ethernet_use_dhcp();
            if (ret == ESP_OK) {
                snprintf(s->config_message, sizeof(s->config_message), "Mode DHCP activé");
                s->config_message_error = false;
            } else {
                snprintf(s->config_message, sizeof(s->config_message),
                         "Erreur DHCP: %s", esp_err_to_name(ret));
                s->config_message_error = true;
            }
        } else if (strcmp(mode, "static") == 0) {
            char ip[20] = {0};
            char mask[20] = {0};
            char gw[20] = {0};

            bool missing =
                httpd_query_key_value(body, "ip", ip, sizeof(ip)) != ESP_OK ||
                httpd_query_key_value(body, "mask", mask, sizeof(mask)) != ESP_OK ||
                httpd_query_key_value(body, "gw", gw, sizeof(gw)) != ESP_OK;

            if (missing) {
                snprintf(s->config_message, sizeof(s->config_message),
                         "IP, masque et gateway sont requis en mode statique");
                s->config_message_error = true;
            } else {
                ret = ethernet_use_static_ip(ip, mask, gw);
                if (ret == ESP_OK) {
                    snprintf(s->config_message, sizeof(s->config_message),
                             "Configuration statique appliquée");
                    s->config_message_error = false;
                } else if (ret == ESP_ERR_INVALID_ARG) {
                    snprintf(s->config_message, sizeof(s->config_message),
                             "Adresse IPv4 ou masque invalide");
                    s->config_message_error = true;
                } else {
                    snprintf(s->config_message, sizeof(s->config_message),
                             "Erreur configuration: %s", esp_err_to_name(ret));
                    s->config_message_error = true;
                }
            }
        } else {
            snprintf(s->config_message, sizeof(s->config_message), "Mode IP invalide");
            s->config_message_error = true;
        }
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t webserver_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 20;
    config.stack_size = 7168;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd_start");

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root), TAG, "register root");

    httpd_uri_t config_uri = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = config_post,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &config_uri), TAG, "register config");

    const char *probe_paths[] = {
        "/generate_204", "/gen_204",
        "/hotspot-detect.html", "/library/test/success.html",
        "/connecttest.txt", "/ncsi.txt",
        "/redirect", "/success.txt"
    };

    for (size_t i = 0; i < sizeof(probe_paths) / sizeof(probe_paths[0]); ++i) {
        httpd_uri_t probe = {
            .uri = probe_paths[i],
            .method = HTTP_GET,
            .handler = captive_redirect,
            .user_ctx = NULL,
        };
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &probe),
                            TAG, "register captive probe");
    }

    httpd_uri_t wildcard = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_redirect,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &wildcard), TAG, "register wildcard");

    ESP_LOGI(TAG, "Web UI + captive portal started");
    return ESP_OK;
}
