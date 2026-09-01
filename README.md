# NetTool V0 — ESP32 + ElectroDragon LAN8720 V2

Première base du futur outil réseau :
- Wi‑Fi SoftAP local
- Ethernet LAN8720 via RMII
- DHCP Ethernet
- page Web de statut
- structure prête pour profils MAC/IP et découverte LLDP/CDP

## Matériel ciblé
ESP32 DevKitC (ESP32 classique, ex. ESP32-D0WDQ6-V3 / WROOM-32) + ElectroDragon NWI1200 V2.

Brochage RMII documenté par ElectroDragon :
- TXD0 GPIO19
- TXD1 GPIO22
- TX_EN GPIO21
- RXD0 GPIO25
- RXD1 GPIO26
- CRS_DV GPIO27
- MDC GPIO23
- MDIO GPIO18
- REF_CLK 50 MHz -> GPIO0 (entrée)
- PHY address 0
- contrôle oscillateur prévu par la carte via GPIO2

Important : garder les jumpers de la NWI1200 V2 dans leur configuration par défaut.

## Version ESP-IDF
Projet écrit pour ESP-IDF 5.x, cible ESP32 classique.

## Compilation
```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Wi‑Fi
Au démarrage :
- SSID : NETTOOL-XXXX
- mot de passe : nettool123
- adresse Web : http://192.168.4.1/

XXXX correspond aux deux derniers octets de la MAC Wi‑Fi.

## V0
La V0 sert volontairement à valider :
1. démarrage ESP32
2. SoftAP Wi‑Fi
3. lien LAN8720
4. DHCP Ethernet
5. serveur Web local

Les fonctions suivantes sont préparées mais pas encore activées :
- changement de MAC Ethernet
- IP fixe
- profils HP / Ricoh / Cisco / PC
- capture / décodage LLDP
- capture / décodage CDP
- écran OLED/TFT

On ajoutera ces fonctions une fois le lien RMII validé sur le matériel réel.


## V0.6 — DHCP Wi-Fi + portail captif

- Le SoftAP utilise le DHCP local créé par `esp_netif_create_default_wifi_ap()`.
- DNS UDP/53 wildcard : les requêtes IPv4/A reçoivent `192.168.4.1`.
- Les URLs de détection Android/iOS/Windows sont redirigées vers l'interface.
- Toute autre URL HTTP est redirigée vers `http://192.168.4.1/`.
- Le Wi-Fi et le Web continuent si le LAN8720 est absent.

Le téléphone peut encore indiquer « Pas d'Internet » : c'est normal, NetTool reste
un réseau local sans accès Internet. Le portail captif facilite surtout l'ouverture
de l'interface et limite le basculement automatique sur certains appareils.


## V0.7 — statut Ethernet + correction serveur Web

- Si le LAN8720 n'est pas disponible, le NetTool continue de démarrer.
- La page Wi-Fi affiche clairement `NON DÉTECTÉ / NON INITIALISÉ` et l'erreur ESP-IDF.
- La page HTML est maintenant envoyée par petits blocs (chunked response) afin d'éviter
  de saturer la pile de la tâche HTTP.
- La pile HTTP a également été portée à 6144 octets pour garder de la marge.
- `main/idf_component.yml` est inclus avec la dépendance `espressif/lan87xx`.
  Sur une nouvelle extraction, `idf.py reconfigure` / `idf.py build` téléchargera donc
  automatiquement le composant géré.


## V0.8 — affichage conditionnel et simplification de l’interface
- La section Découverte réseau n'est plus affichée lorsque le module Ethernet n'est pas disponible.
- Si le LAN8720 n'est pas détecté ou ne peut pas être initialisé, l'interface affiche uniquement :
- l'état du Wi-Fi ;
- l'état du module Ethernet ;
- l'erreur d'initialisation éventuelle.
- Si le module Ethernet est présent mais qu'aucun câble réseau n'est connecté, la section Découverte réseau reste également masquée.
- La section Découverte réseau apparaît uniquement lorsque :
- le module Ethernet est correctement initialisé ;
- le lien Ethernet est UP.
- L'affichage est donc maintenant adapté automatiquement selon l'état réel du matériel :
- Ethernet absent → Wi-Fi + erreur Ethernet ;
- Ethernet présent / Link DOWN → Wi-Fi + état Ethernet ;
- Ethernet présent / Link UP → Wi-Fi + Ethernet + découverte réseau.
- Le buffer temporaire utilisé pour générer les informations HTML a été augmenté à 512 octets afin d'éviter les erreurs format-truncation lors de la compilation.
- Le serveur Web conserve l'envoi de la page HTML par petits blocs (chunked response) afin de limiter l'utilisation de la pile.
- La pile de la tâche HTTP reste configurée à 6144 octets.
- Le portail captif et le serveur DHCP Wi-Fi restent actifs même en l'absence du module Ethernet.
- Le NetTool reste donc administrable en Wi-Fi afin de pouvoir diagnostiquer un problème Ethernet ou un module LAN8720 absent.