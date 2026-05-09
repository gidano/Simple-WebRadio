# Simple WebRadio / Egyszerű WebRádió

Simple ESP32 Web Radio with Arduino IDE  
Egyszerű ESP32 alapú internetes rádió Arduino IDE használatával

<p align="center">
  <!--
  <img src="https://img.shields.io/github/downloads/gidano/Simple-WebRadio/total?style=for-the-badge&cacheSeconds=60" alt="Total Downloads">   -->
  <img src="https://img.shields.io/github/stars/gidano/Simple-WebRadio?style=for-the-badge" alt="Stars">
  <img src="https://img.shields.io/github/repo-size/gidano/Simple-WebRadio?style=for-the-badge" alt="Repo size">
</p>

Managing Radio Stations Based on the Community Radio Station Index:  
Rádióállomások kezelése a Community Radio Station Index alapján:  
https://de1.api.radio-browser.info/

---

# Screenshot / Képernyőképek

<p align="center">
  <img src="https://github.com/gidano/Simple-WebRadio/blob/main/Photos/display.jpg" width="200"><br><br>
  <img src="https://github.com/gidano/Simple-WebRadio/blob/main/Photos/radio_front.jpg" width="600">
</p>

---

# Hardware / Hardver

- ESP32-S3 Supermini  
  https://www.aliexpress.com/item/1005006960134338.html

- ST7735 128x160 4SPI RGB TFT display v1 - v2  
  https://www.aliexpress.com/item/1005009712175802.html

- KY-040 or EC11 rotary encoder / KY-040 vagy EC11 forgó enkóder  
  https://www.aliexpress.com/w/wholesale-encoder-rotary-arduino.html

- or 3 buttons panel / vagy 3 gombos panel  
  https://www.aliexpress.com/w/wholesale-3-buttons-module.html

- DAC 5102a  
  https://www.aliexpress.com/w/wholesale-pcm5102.html

- or MAX98357a I2S amplifier / vagy MAX98357a I2S erősítő  
  https://www.aliexpress.com/w/wholesale-max98357a-i2s-amplifier.html

---

# Software Features / Szoftver funkciók

- Significantly modified regarding station management.  
  Jelentősen módosított állomáskezelés.

- Instead of only 3–4 stations per country, stations are grouped into pages of 28 entries (P1–Pxx).  
  A korábbi országonkénti 3–4 állomás helyett az állomások most 28-as csoportokba rendezve, külön oldalakon (P1–Pxx) jelennek meg.

- PAUSE text appears on the display when playback is paused.  
  Lejátszás szüneteltetésekor PAUSE felirat jelenik meg a kijelzőn.

- Expanded with Hungarian radio station management.  
  Magyar rádióállomások támogatásával bővítve.

- Backlight automatically dims after 1 minute to extend battery life.  
  A háttérvilágítás 1 perc után automatikusan csökken az akkumulátoros üzemidő növelése érdekében.

```cpp
#define BACKLIGHT_FULL      200     // 0-255
#define BACKLIGHT_DIM       50      // 0-255
#define BACKLIGHT_DIM_AFTER 60000UL // 1 minute
```

- You can customize the brightness values before flashing the firmware.  
  A fényerő értékek a firmware feltöltése előtt szabadon módosíthatók.

- This only works if the display BL-LED pin is connected to a controllable GPIO pin.  
  Ez csak akkor működik, ha a kijelző BL-LED lába egy vezérelhető GPIO pinre van kötve.

- Long station names automatically scroll during station selection for better readability.  
  A hosszú állomásnevek automatikusan görgetve jelennek meg az olvashatóság érdekében.

- The green circle in the bottom-right corner expands when holding the play/pause/menu button for 700 ms.  
  A jobb alsó sarokban található zöld kör 700 ms nyomva tartás után kitágul.

- This area displays the current button or encoder action.  
  Ebben a mezőben látható az aktuális gomb- vagy enkóder művelet.

- Supports both rotary encoder and 3-button control.  
  Forgó enkóderes és 3 gombos vezérlést is támogat.

- The current volume level is displayed in the top status bar.  
  Az aktuális hangerő a felső státuszsávban jelenik meg.

- The center of the screen displays current track information.  
  A kijelző közepén az aktuálisan lejátszott szám információi láthatók.

- The selected country and category are displayed in the top-left corner.  
  A bal felső sarokban a kiválasztott ország és kategória jelenik meg.

- Available categories include: Music, News, Jazz, Classical, Rock, and All.  
  Elérhető kategóriák: Music, News, Jazz, Classical, Rock és All.

---

# Country List / Országlista

The current country list begins around line 54 in the source code.  
Az aktuális országlista körülbelül az 54. sortól található a forráskódban.

```cpp
static const CountryEntry COUNTRIES[] = {
  { "all", "All" },
  { "HU", "Hungary" },
  { "US", "USA" },
  { "IN", "India" },
  { "GB", "UK" },
  { "DE", "Germany" },
  { "FR", "France" },
  { "JP", "Japan" },
  { "CA", "Canada" },
  { "AU", "Australia" },
  { "IT", "Italy" },
  { "ES", "Spain" },
  { "BR", "Brazil" },
  { "MX", "Mexico" },
  { "NL", "Netherlands" },
  { "SE", "Sweden" },
  { "NO", "Norway" },
  { "ZA", "South Africa" },
  { "SG", "Singapore" },
  { "AE", "UAE" },
};
```

---

# Installation / Telepítés

- Before uploading the firmware, open the `secrets.h` file and enter your WiFi SSID and password.  
  A firmware feltöltése előtt nyisd meg a `secrets.h` fájlt, és add meg a saját WiFi SSID és jelszó adatokat.

- Configure the ESP32-S3 Supermini according to the settings shown in the `Setup_Arduino_IDE` folder.  
  Állítsd be az ESP32-S3 Supermini modult a `Setup_Arduino_IDE` mappában található képek alapján.

- Important: PSRAM must be enabled.  
  Fontos: a PSRAM engedélyezése kötelező.

---

# Troubleshooting / Hibaelhárítás

- If the display appears shifted after installation, or you see 1–2 pixel wide stripes on the edges, adjust the display offset settings around line 110 in the `.ino` file.  
  Ha a kijelző elcsúszva jelenik meg, vagy 1–2 pixeles csíkok látszanak a széleken, módosítsd az offset beállításokat a `.ino` fájl körülbelül 110. sorában.

```cpp
setColRowStart(0, 0); // V2 blue panel: 2px column + 1px row offset
```

---

# Credits / Köszönet

Fork based on / Az alapprojekt:  
TechTalkies Internet Radio  
https://github.com/TechTalkies/YouTube/tree/main/101%20Internet%20Radio%20V2

