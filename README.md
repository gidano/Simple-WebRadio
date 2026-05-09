# Simple WebRadio
Simple ESP32 Web Radio with Arduino IDE

<p align="center">
  <!-- 
  <img src="https://img.shields.io/github/downloads/gidano/Simple-WebRadio/total?style=for-the-badge&cacheSeconds=60" alt="Total Downloads">   -->
  <img src="https://img.shields.io/github/stars/gidano/Simple-WebRadio?style=for-the-badge" alt="Stars">
  <img src="https://img.shields.io/github/repo-size/gidano/Simple-WebRadio?style=for-the-badge" alt="Repo size">
</p>

Managing Radio Stations Based on the Community Radio Station Index: https://de1.api.radio-browser.info/

## Screenshot
<p align="center">
  <img src="https://github.com/gidano/Simple-WebRadio/blob/main/Photos/display.jpg" width="200"><br><br>
  <img src="https://github.com/gidano/Simple-WebRadio/blob/main/Photos/radio_front.jpg" width="600">
</p>

Hardware:
- ESP32-S3 Supermini  -  https://www.aliexpress.com/item/1005006960134338.html
- ST7735 128x160 4SPI RGB TFT display v1 - v2  -  https://www.aliexpress.com/item/1005009712175802.html
- KY-040 or EC11 rotary encoder - https://www.aliexpress.com/w/wholesale-encoder-rotary-arduino.html
- or 3 buttons panel - https://www.aliexpress.com/w/wholesale-3-buttons-module.html
- DAC 5102a - https://www.aliexpress.com/w/wholesale-pcm5102.html
- or MAX98357a i2s amplifier - https://www.aliexpress.com/w/wholesale-max98357a-i2s-amplifier.html

Software:
- Significantly modified with regard to managed stations.
  Instead of 3–4 available stations per country, available stations are now grouped in sets of 28 on separate pages (P1–Pxx).
- PAUSE text on the display when playback is paused.
- Expanded to include management of Hungarian stations. 
- Backlight dims by 20% after 1 minutes of operation to extend battery life. You can also set a custom value before the flash, around line 32:
      #define BACKLIGHT_FULL      200     //  0-255
      #define BACKLIGHT_DIM       50      //  0-255
      #define BACKLIGHT_DIM_AFTER 60000UL  // 1 minutes
  (This only works if you connect the display's BL-LED pin to an open pin.
  If these pins are directly powered, the software modification will not work.)
- When selecting a station, scroll through names that are longer than the name field for better readability.
- The circle in the lower-right corner is an green indicator that expands when you press and hold the play/pause/menu button (700 ms). In this field, you can see the current information from the buttons (or encoder).
- In addition to the existing settings, you can also use an encoder or a 3-button solution!
- The volume level is displayed in the top horizontal field.
- In the center of the main screen is information about the currently playing track; in the left corner is the selected country (or ALL), and next to it are the genre  categories: Music, News, Jazz, Classical, Rock (or All).
- The current list of countries starts around line 54 in the code:
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

Installing:
- Before uploading, open the secrets.h file located next to the .ino file and fill in the WiFi SSID and Password fields with your own information.
- Configure the ESP32-S3 Supermini with the desired settings based on the photo in the Setup_Arduino_IDE folder. Remember: PSRAM must be enabled!.

Troubleshooting:
- If you encounter a panel where, after installation, the screen appears misaligned and/or you see 1-2px-wide stripes at the top/bottom or left/right edges, you can correct this by adjusting the offset setting around line 110 of the .ino file:
"setColRowStart(0, 0);    // V2 at the blue panel: 2px col + 1px row offset"

Fork: TechTalkies Internet Radio  -  https://github.com/TechTalkies/YouTube/tree/main/101%20Internet%20Radio%20V2
