# Simple WebRadio
Simple ESP32 Web Radio with Arduino IDE

Fork: TechTalkies Internet Radio  -  https://github.com/TechTalkies/YouTube/tree/main/101%20Internet%20Radio%20V2

Rádióállomások kezelése a Community Radio Station Index alapján: https://de1.api.radio-browser.info/

Hardware:
- ESP32-S3 Supermini  -  https://www.aliexpress.com/item/1005006960134338.html
- ST7735 128x160 4SPI RGB TFT display V1  -  https://www.aliexpress.com/item/1005009712175802.html

Software:
- Significantly modified with regard to managed stations
  Instead of 3–4 available stations per country, available stations are now grouped in sets of 28 on separate tabs (P1–Pxx)
- PAUSE text on the display when playback is paused
- Expanded to include management of Hungarian stations 
- Backlight dims by 50% after 3 minutes of operation to extend battery life
  (This only works if you connect the display's BL-LED pin to an open pin.
  If these pins are directly powered, the software modification will not work.)
- When selecting a station, scroll through names that are longer than the name field for better readability
- The circle in the lower-right corner is an green indicator that expands when you press and hold the play/pause/menu button (700 ms)
- In addition to the existing settings, you can also use an encoder or a 3-button solution!
- The volume level is displayed in the top horizontal field
- In the center of the main screen is information about the currently playing track; in the left corner is the selected country (or ALL), and next to it are the genre  categories: Music, News, Jazz, Classical, Rock (or All)

Installing:
- Before uploading, open the secrets.h file located next to the .ino file and fill in the WiFi SSID and Password fields with your own information
- Configure the ESP32-S3 Supermini with the desired settings based on the photo in the Setup_Arduino_IDE folder. Remember: PSRAM must be enabled!
