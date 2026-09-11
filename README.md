# NetTool — ESP32-S3 + W5500

NetTool est un petit outil réseau portable basé sur la carte **Waveshare ESP32-S3-ETH / PoE**.
Il crée son propre Wi-Fi de gestion et utilise le contrôleur Ethernet W5500 intégré pour tester une prise réseau.

## État actuel — V0.9

Fonctions disponibles :

- Wi-Fi SoftAP `NETTOOL-XXXX`
- portail captif local sur `http://192.168.4.1/`
- Ethernet W5500 10/100 Mbps via SPI
- affichage Link / vitesse / duplex / MAC
- choix **DHCP ou IPv4 statique** depuis l'interface Web
- configuration IP / masque / gateway
- sauvegarde de la configuration IPv4 en NVS
- DHCP Ethernet
- écoute passive **LLDP et CDP**
- affichage des informations de switch lorsqu'elles sont annoncées :
  - protocole LLDP/CDP
  - nom du switch
  - port
  - VLAN/PVID lorsque présent
  - IP de management lorsque présente
  - plateforme / description lorsque disponibles

Le Wi-Fi de gestion reste disponible même si le câble Ethernet est débranché.

## Matériel

Carte actuelle : **Waveshare ESP32-S3-ETH / PoE**

| Fonction W5500 | GPIO ESP32-S3 |
|---|---:|
| MOSI | GPIO11 |
| MISO | GPIO12 |
| SCLK | GPIO13 |
| CS | GPIO14 |
| RESET | GPIO9 |
| INT | GPIO10 |

Le bus Ethernet utilise `SPI2_HOST`.

## ESP-IDF

Projet actuellement prévu pour :

```text
ESP-IDF 6.0.2
Target: esp32s3
```

Dépendance Ethernet :

```yaml
dependencies:
  espressif/w5500: "^2.0.0"
```

## Compilation

Dans un terminal ESP-IDF :

```bat
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Après une modification importante de cible ou de dépendances :

```bat
idf.py fullclean
idf.py reconfigure
idf.py build
```

## Wi-Fi de gestion

Au démarrage :

- SSID : `NETTOOL-XXXX`
- mot de passe : `nettool123`
- interface : `http://192.168.4.1/`

`XXXX` correspond aux deux derniers octets de la MAC Wi-Fi.

Le téléphone peut afficher « Pas d'Internet ». C'est normal : le SoftAP sert uniquement à administrer NetTool localement.

## Configuration IPv4 Ethernet

L'interface Web permet maintenant de choisir :

### DHCP

Le W5500 demande automatiquement une adresse au serveur DHCP du réseau.

### Statique

L'utilisateur peut définir :

- adresse IPv4
- masque
- gateway

La configuration est enregistrée dans la NVS de l'ESP32-S3 et reste présente après redémarrage.

## Découverte LLDP / CDP

NetTool inspecte passivement les trames Ethernet reçues par le W5500 avant de les transmettre normalement à la pile TCP/IP.

### LLDP

NetTool reconnaît notamment :

- System Name
- Port ID
- Port Description
- System Description
- Management IPv4
- IEEE 802.1 PVID lorsqu'il est annoncé

LLDP utilise :

```text
Destination MAC : 01:80:C2:00:00:0E
EtherType       : 0x88CC
```

### CDP

NetTool reconnaît notamment :

- Device ID
- Port ID
- Management IPv4
- Software Version
- Platform
- Native VLAN

CDP utilise :

```text
Destination MAC : 01:00:0C:CC:CC:CC
LLC/SNAP Cisco  : AA AA 03 00 00 0C 20 00
```

Selon la configuration du switch, les annonces LLDP/CDP peuvent prendre plusieurs dizaines de secondes avant d'apparaître.
Si LLDP et CDP sont tous les deux disponibles, NetTool privilégie LLDP et utilise CDP pour compléter certaines informations Cisco.

## Architecture

```text
Téléphone
   |
   | Wi-Fi
   v
+------------------------+
|       ESP32-S3         |
|                        |
|  SoftAP + Web UI       |
|                        |
|  ESP-NETIF / lwIP      |
|          |             |
|   inspection LLDP/CDP  |
|          |             |
|        SPI2            |
+----------+-------------+
           |
           v
        +------+
        | W5500|
        +--+---+
           |
          RJ45
           |
         Switch
```

## Historique

### V0

Première base avec ESP32 classique + ElectroDragon LAN8720 V2 via RMII :

- SoftAP Wi-Fi
- Ethernet RMII
- DHCP
- serveur Web

### V0.6

- DHCP du SoftAP
- DNS wildcard
- portail captif Android / iOS / Windows
- fonctionnement Wi-Fi même sans Ethernet

### V0.7

- gestion propre des erreurs Ethernet
- réponses HTTP chunked
- pile HTTP augmentée

### V0.8

- affichage conditionnel selon l'état Ethernet
- interface simplifiée
- préparation des structures de profils et de découverte

### V0.9

Migration et amélioration de la nouvelle plateforme :

- ESP32-S3
- W5500 SPI intégré
- support de la carte PoE
- MAC Ethernet déterministe dérivée de l'ESP32-S3
- DHCP / IPv4 statique configurable depuis le Web
- paramètres IPv4 persistants en NVS
- capture passive des trames Ethernet
- décodage LLDP
- décodage CDP
- informations du switch dans l'interface Web

## Suite prévue

- changement de MAC depuis l'interface Web
- profils PC / HP / Ricoh / Cisco
- MAC personnalisée
- DNS statique
- affichage plus détaillé des TLV LLDP/CDP
- historique des changements de port / VLAN
- éventuel écran local

## Objectif

À terme, le branchement du NetTool sur une prise doit fournir rapidement quelque chose comme :

```text
Link       : UP
Speed      : 100 Mbps FULL

Switch     : LMB-SW-ACCESS01
Port       : Gi1/0/24
VLAN       : 170
Mgmt IP    : 10.27.1.15
Protocol   : LLDP

IP mode    : DHCP
IP         : 10.27.170.53
Mask       : 255.255.254.0
Gateway    : 10.27.170.1
MAC        : xx:xx:xx:xx:xx:xx
```

Le tout doit rester utilisable depuis un téléphone, sans PC et sans connexion Internet.
