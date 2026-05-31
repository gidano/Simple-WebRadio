# Smart WebRadio T-Display S3

[English](#english) | [Magyar](#magyar)

---

## English

**Smart WebRadio T-Display S3** is a compact ESP32-S3 based internet radio firmware for the LILYGO T-Display S3.  
It plays online radio streams over Wi-Fi, shows station and stream information on the built-in display, and uses an external I2S DAC for audio output.

### Main features

- ESP32-S3 / LILYGO T-Display S3 support
- ST7789 320×170 display UI
- Internet radio playback over Wi-Fi
- Station list loaded from SPIFFS
- PCM5102A I2S DAC audio output
- Basic stream metadata display when available
- Volume and station control using the board buttons
- Built-in web endpoints for station list handling
- Data files stored in the `/data` folder and uploaded to SPIFFS

### How it works

The radio connects to a configured Wi-Fi network, loads the station list from SPIFFS, and starts playing the selected stream through the I2S DAC.  
The display shows the current station, stream information, volume state, Wi-Fi status, and playback/buffer status.

Stations are stored in:

```text
/data/stations.txt
```

The `/data` folder can be uploaded to the root of the SPIFFS partition with the LittleFS-SPIFFS Partition Manager:

https://github.com/gidano/myRadio-SPIFFS-Manager/tree/main/LittleFS-SPIFFS%20Partition%20Manager

### Hardware notes

Recommended audio wiring for a PCM5102A DAC:

```text
BCK  -> GPIO 2
LRCK -> GPIO 3
DIN  -> GPIO 1
SCK  -> GND
FMT  -> GND
XMT  -> 3.3V
```

The firmware is intended for Arduino IDE based builds.

---

## Magyar

A **Smart WebRadio T-Display S3** egy ESP32-S3 alapú, kompakt internetes rádió firmware a LILYGO T-Display S3 panelhez.  
Wi-Fi-n keresztül online rádióadásokat játszik le, a beépített kijelzőn megjeleníti az állomás és stream információkat, a hangkimenethez pedig külső I2S DAC-ot használ.

### Főbb funkciók

- ESP32-S3 / LILYGO T-Display S3 támogatás
- ST7789 320×170 kijelzős felület
- Internetes rádiólejátszás Wi-Fi-n keresztül
- Állomáslista SPIFFS partícióról
- PCM5102A I2S DAC hangkimenet
- Alap stream metaadat megjelenítés, ha az adó küldi
- Hangerő és állomás vezérlés a panel gombjaival
- Beépített webes végpontok az állomáslista kezeléséhez
- A szükséges adatfájlok a `/data` mappában találhatók

### Működés röviden

A rádió csatlakozik a beállított Wi-Fi hálózathoz, betölti az állomáslistát az SPIFFS partícióról, majd a kiválasztott streamet az I2S DAC-on keresztül lejátssza.  
A kijelzőn látható az aktuális állomás, a stream információ, a hangerő, a Wi-Fi állapot, valamint a lejátszás/puffer állapota.

Az állomáslista helye:

```text
/data/stations.txt
```

A `/data` mappa feltöltése az SPIFFS partíció gyökerébe a LittleFS-SPIFFS Partition Manager használatával lehetséges:

https://github.com/gidano/myRadio-SPIFFS-Manager/tree/main/LittleFS-SPIFFS%20Partition%20Manager

### Hardver megjegyzés

Ajánlott PCM5102A DAC bekötés:

```text
BCK  -> GPIO 2
LRCK -> GPIO 3
DIN  -> GPIO 1
SCK  -> GND
FMT  -> GND
XMT  -> 3.3V
```

A firmware Arduino IDE alapú fordításhoz készült.
