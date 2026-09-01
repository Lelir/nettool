# NetTool — ESP32-S3 Ethernet

NetTool est un outil réseau portable basé sur ESP32, destiné au diagnostic rapide d'une prise réseau.

Le projet fournit actuellement :

- Wi-Fi SoftAP local
- portail captif
- interface Web locale
- Ethernet 10/100 Mbps
- DHCP Ethernet
- affichage de l'état du lien
- affichage IP / masque / gateway
- affichage vitesse et duplex
- base prête pour profils MAC/IP
- base prête pour découverte LLDP/CDP

Le projet était initialement développé avec un ESP32 classique et un module LAN8720 RMII.

À partir de la nouvelle version matérielle, NetTool utilise une carte Waveshare ESP32-S3-ETH avec contrôleur Ethernet W5500 intégré.

---

## Matériel actuel

### Waveshare ESP32-S3-ETH / PoE

- MCU : ESP32-S3
- Ethernet : W5500
- Interface Ethernet : SPI
- Ethernet : 10/100 Mbps
- alimentation possible par USB
- alimentation PoE sur la version PoE

Le W5500 est directement intégré à la carte : aucun module Ethernet externe ou câblage RMII n'est nécessaire.

### Brochage W5500

| Fonction | GPIO |
|---|---:|
| MOSI | GPIO11 |
| MISO | GPIO12 |
| SCLK | GPIO13 |
| CS | GPIO14 |
| RESET | GPIO9 |
| INT | GPIO10 |

Le bus Ethernet utilise `SPI2_HOST`.

---

## Architecture

```text
                    +----------------------+
                    |      ESP32-S3        |
                    |                      |
Téléphone ----------| Wi-Fi SoftAP         |
192.168.4.x          |                      |
                    | Web / Captive Portal |
                    |                      |
                    | SPI2                 |
                    +----------+-----------+
                               |
                     GPIO 9-14 |
                               |
                         +-----v-----+
                         |   W5500   |
                         +-----+-----+
                               |
                              RJ45
                               |
                         Réseau Ethernet
