#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "nettool_state.h"
#include "webserver.h"

static const char *TAG = "webserver";


/*
 * Envoie un morceau de page HTML.
 * Cela évite d'avoir un énorme buffer HTML sur la pile.
 */
static esp_err_t send_chunk(httpd_req_t *req, const char *text)
{
    return httpd_resp_send_chunk(
        req,
        text,
        HTTPD_RESP_USE_STRLEN
    );
}


/*
 * Page principale du NetTool
 */
static esp_err_t root_get(httpd_req_t *req)
{
    nettool_state_t *s = nettool_state_get();

    char ip[16] = "-";
    char mask[16] = "-";
    char gw[16] = "-";

    /*
     * Buffer temporaire pour générer les petites parties dynamiques
     * de la page.
     */
    char line[512];


    /*
     * Si Ethernet possède une adresse IP,
     * on prépare les chaînes IP / masque / gateway.
     */
    if (s->eth_has_ip) {

        snprintf(
            ip,
            sizeof(ip),
            IPSTR,
            IP2STR(&s->eth_ip.ip)
        );

        snprintf(
            mask,
            sizeof(mask),
            IPSTR,
            IP2STR(&s->eth_ip.netmask)
        );

        snprintf(
            gw,
            sizeof(gw),
            IPSTR,
            IP2STR(&s->eth_ip.gw)
        );
    }


    /*
     * Headers HTTP
     */
    httpd_resp_set_type(
        req,
        "text/html; charset=utf-8"
    );

    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store"
    );


    /*
     * Début de la page
     */
    ESP_RETURN_ON_ERROR(
        send_chunk(
            req,

            "<!doctype html>"
            "<html>"
            "<head>"

            "<meta charset='utf-8'>"

            "<meta name='viewport' "
            "content='width=device-width,initial-scale=1'>"

            /*
             * Recharge automatiquement la page toutes les 3 secondes.
             * Plus tard on remplacera probablement ça par une API AJAX.
             */
            "<meta http-equiv='refresh' content='3'>"

            "<title>NETTOOL</title>"

            "<style>"

            "body{"
                "font-family:system-ui,-apple-system,sans-serif;"
                "margin:0;"
                "background:#111;"
                "color:#eee;"
            "}"

            ".w{"
                "max-width:760px;"
                "margin:auto;"
                "padding:20px;"
            "}"

            "h1{"
                "margin-bottom:20px;"
            "}"

            ".c{"
                "background:#1d1d1d;"
                "border-radius:14px;"
                "padding:18px;"
                "margin:12px 0;"
            "}"

            ".ok{"
                "color:#67e480;"
                "font-weight:600;"
            "}"

            ".bad{"
                "color:#ff7b72;"
                "font-weight:600;"
            "}"

            ".warn{"
                "color:#f2c94c;"
                "font-weight:600;"
            "}"

            "table{"
                "width:100%;"
                "border-collapse:collapse;"
            "}"

            "td{"
                "padding:8px;"
                "border-bottom:1px solid #333;"
            "}"

            "td:first-child{"
                "color:#aaa;"
                "width:42%;"
            "}"

            ".hint{"
                "color:#aaa;"
                "font-size:.92rem;"
                "line-height:1.4;"
            "}"

            "</style>"

            "</head>"

            "<body>"

            "<div class='w'>"

            "<h1>NETTOOL V0.8</h1>"
        ),

        TAG,
        "send page header"
    );


    /*
     * =========================================================
     * Wi-Fi
     * =========================================================
     */
    ESP_RETURN_ON_ERROR(
        send_chunk(
            req,

            "<div class='c'>"

            "<h2>Wi-Fi</h2>"

            "<table>"

            "<tr>"
                "<td>Portail captif</td>"
                "<td class='ok'>ACTIF</td>"
            "</tr>"

            "<tr>"
                "<td>Adresse</td>"
                "<td>192.168.4.1</td>"
            "</tr>"

            "</table>"

            "</div>"
        ),

        TAG,
        "send wifi state"
    );


    /*
     * =========================================================
     * Ethernet
     * =========================================================
     */

    ESP_RETURN_ON_ERROR(
        send_chunk(
            req,

            "<div class='c'>"

            "<h2>Ethernet</h2>"

            "<table>"
        ),

        TAG,
        "send ethernet header"
    );


    /*
     * ---------------------------------------------------------
     * PHY / Ethernet non initialisé
     * ---------------------------------------------------------
     *
     * Exemple actuellement lorsque le LAN8720 n'est pas branché.
     */
    if (!s->eth_initialized) {

        snprintf(
            line,
            sizeof(line),

            "<tr>"
                "<td>Module Ethernet</td>"
                "<td class='bad'>NON DÉTECTÉ / NON INITIALISÉ</td>"
            "</tr>"

            "<tr>"
                "<td>Erreur</td>"
                "<td>%s</td>"
            "</tr>"

            "<tr>"
                "<td>Link</td>"
                "<td class='bad'>INDISPONIBLE</td>"
            "</tr>",

            esp_err_to_name(s->eth_init_error)
        );


        ESP_RETURN_ON_ERROR(
            send_chunk(
                req,
                line
            ),

            TAG,
            "send ethernet missing"
        );


        /*
         * On ferme le tableau Ethernet.
         */
        ESP_RETURN_ON_ERROR(
            send_chunk(
                req,

                "</table>"

                "<p class='hint'>"

                "Le module Ethernet n'est pas disponible. "
                "Le NetTool reste entièrement accessible en Wi-Fi."

                "</p>"

                "</div>"

                "</div>"

                "</body>"
                "</html>"
            ),

            TAG,
            "send ethernet missing footer"
        );


        /*
         * Fin de la réponse.
         *
         * IMPORTANT :
         * on ne montre PAS la section découverte.
         */
        return httpd_resp_send_chunk(
            req,
            NULL,
            0
        );
    }


    /*
     * ---------------------------------------------------------
     * Ethernet initialisé
     * ---------------------------------------------------------
     */

    snprintf(
        line,
        sizeof(line),

        "<tr>"
            "<td>Module Ethernet</td>"
            "<td class='ok'>INITIALISÉ</td>"
        "</tr>"

        "<tr>"
            "<td>Link</td>"
            "<td class='%s'>%s</td>"
        "</tr>"

        "<tr>"
            "<td>Vitesse</td>"
            "<td>%d Mbps %s</td>"
        "</tr>"

        "<tr>"
            "<td>MAC</td>"
            "<td>%02X:%02X:%02X:%02X:%02X:%02X</td>"
        "</tr>",

        s->eth_link ? "ok" : "warn",

        s->eth_link
            ? "UP"
            : "DOWN",

        s->eth_speed_mbps,

        s->eth_full_duplex
            ? "FULL"
            : "HALF",

        s->eth_mac[0],
        s->eth_mac[1],
        s->eth_mac[2],
        s->eth_mac[3],
        s->eth_mac[4],
        s->eth_mac[5]
    );


    ESP_RETURN_ON_ERROR(
        send_chunk(
            req,
            line
        ),

        TAG,
        "send ethernet state"
    );


    /*
     * IP Ethernet
     */
    snprintf(
        line,
        sizeof(line),

        "<tr>"
            "<td>IP</td>"
            "<td>%s</td>"
        "</tr>"

        "<tr>"
            "<td>Masque</td>"
            "<td>%s</td>"
        "</tr>"

        "<tr>"
            "<td>Gateway</td>"
            "<td>%s</td>"
        "</tr>"

        "</table>",

        ip,
        mask,
        gw
    );


    ESP_RETURN_ON_ERROR(
        send_chunk(
            req,
            line
        ),

        TAG,
        "send ethernet ip"
    );


    /*
     * ---------------------------------------------------------
     * Module présent MAIS câble débranché
     * ---------------------------------------------------------
     *
     * On affiche Ethernet mais PAS découverte.
     */
    if (!s->eth_link) {

        ESP_RETURN_ON_ERROR(
            send_chunk(
                req,

                "<p class='hint'>"

                "Le module Ethernet est présent, "
                "mais aucun lien réseau n'est détecté."

                "</p>"

                "</div>"

                "</div>"

                "</body>"
                "</html>"
            ),

            TAG,
            "send link down footer"
        );


        return httpd_resp_send_chunk(
            req,
            NULL,
            0
        );
    }


    /*
     * =========================================================
     * Découverte réseau
     * =========================================================
     *
     * Cette section apparaît uniquement si :
     *
     * Ethernet initialisé
     * ET
     * Link UP
     */

    ESP_RETURN_ON_ERROR(
        send_chunk(
            req,

            "</div>"

            "<div class='c'>"

            "<h2>Découverte réseau</h2>"

            "<table>"
        ),

        TAG,
        "send discovery header"
    );


    snprintf(
        line,
        sizeof(line),

        "<tr>"
            "<td>Source</td>"
            "<td>%s</td>"
        "</tr>"

        "<tr>"
            "<td>Switch</td>"
            "<td>%s</td>"
        "</tr>"

        "<tr>"
            "<td>Port</td>"
            "<td>%s</td>"
        "</tr>"

        "<tr>"
            "<td>VLAN</td>"
            "<td>%s</td>"
        "</tr>"

        "</table>"

        "</div>"

        "</div>"

        "</body>"
        "</html>",

        s->discovery_source[0]
            ? s->discovery_source
            : "-",

        s->switch_name[0]
            ? s->switch_name
            : "-",

        s->switch_port[0]
            ? s->switch_port
            : "-",

        s->vlan[0]
            ? s->vlan
            : "-"
    );


    ESP_RETURN_ON_ERROR(
        send_chunk(
            req,
            line
        ),

        TAG,
        "send discovery"
    );


    /*
     * Termine la réponse HTTP chunked.
     */
    return httpd_resp_send_chunk(
        req,
        NULL,
        0
    );
}


/*
 * =============================================================
 * Captive portal
 * =============================================================
 */

static esp_err_t captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(
        req,
        "302 Found"
    );

    httpd_resp_set_hdr(
        req,
        "Location",
        "http://192.168.4.1/"
    );

    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store"
    );

    return httpd_resp_send(
        req,
        NULL,
        0
    );
}


/*
 * =============================================================
 * Démarrage serveur Web
 * =============================================================
 */

esp_err_t webserver_start(void)
{
    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();


    /*
     * Port HTTP standard
     */
    config.server_port = 80;


    /*
     * Nombre de routes disponibles
     */
    config.max_uri_handlers = 16;


    /*
     * Un peu plus de RAM pour la tâche HTTP.
     */
    config.stack_size = 6144;


    /*
     * Permet la route wildcard
     */
    config.uri_match_fn =
        httpd_uri_match_wildcard;


    httpd_handle_t server = NULL;


    /*
     * Démarre le serveur
     */
    ESP_RETURN_ON_ERROR(
        httpd_start(
            &server,
            &config
        ),

        TAG,
        "httpd_start"
    );


    /*
     * Page principale
     */
    httpd_uri_t root = {

        .uri = "/",

        .method = HTTP_GET,

        .handler = root_get,

        .user_ctx = NULL
    };


    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(
            server,
            &root
        ),

        TAG,
        "register root"
    );


    /*
     * =========================================================
     * URLs utilisées par les téléphones / PC
     * pour vérifier la présence d'Internet.
     * =========================================================
     */

    const char *probe_paths[] = {

        /*
         * Android
         */
        "/generate_204",
        "/gen_204",

        /*
         * Apple
         */
        "/hotspot-detect.html",
        "/library/test/success.html",

        /*
         * Windows
         */
        "/connecttest.txt",
        "/ncsi.txt",

        /*
         * Autres
         */
        "/redirect",
        "/success.txt"
    };


    /*
     * Création des routes
     */
    for (
        size_t i = 0;
        i < sizeof(probe_paths) / sizeof(probe_paths[0]);
        i++
    ) {

        httpd_uri_t probe = {

            .uri = probe_paths[i],

            .method = HTTP_GET,

            .handler = captive_redirect,

            .user_ctx = NULL
        };


        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(
                server,
                &probe
            ),

            TAG,
            "register captive probe"
        );
    }


    /*
     * =========================================================
     * Catch-all
     * =========================================================
     *
     * Si quelqu'un ouvre par exemple :
     *
     * http://google.com
     *
     * le DNS NetTool donnera 192.168.4.1,
     * puis cette route le renverra vers /
     */

    httpd_uri_t wildcard = {

        .uri = "/*",

        .method = HTTP_GET,

        .handler = captive_redirect,

        .user_ctx = NULL
    };


    ESP_RETURN_ON_ERROR(
        httpd_register_uri_handler(
            server,
            &wildcard
        ),

        TAG,
        "register wildcard"
    );


    ESP_LOGI(
        TAG,
        "Web UI + captive portal started"
    );


    return ESP_OK;
}