/* -------------------------------------------------
Copyright (c)
TechTalkies Internet Radio
https://github.com/TechTalkies/YouTube/tree/main/101%20Internet%20Radio%20V2

Fork by gidano | https://github.com/gidano/Simple-WebRadio
-------------------------------------------------*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Audio.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <Preferences.h>
#include <WebServer.h>
#include <mbedtls/base64.h>
#include "src/ui/ui_160x128.h"
#include "src/ui/ui_320x170.h"

// ── T-Display S3 (LILYGO) port ───────────────────────────────────────────────
// Kijelző: ST7789V 170x320, 8-bites PÁRHUZAMOS busz (nem SPI!)
// Foglalt GPIO-k (kijelző): 5,6,7,8,9,15,38,39,40,41,42,45,46,47,48
// Foglalt GPIO-k (gombok):  0 (BOOT), 14 (KEY)
// Foglalt GPIO-k (akku):    4
// Szabad GPIO-k: 1, 2, 3, 10, 11, 12, 13, 16, 17, 18, 21, 43, 44
//
// PCM5102A I2S DAC bekötés (szabad pinekre):
//   BCK  → GPIO 2
//   LRCK → GPIO 3
//   DIN  → GPIO 1
//   SCK  → GND (PCM5102 slave mode)
//   FMT  → GND (I2S format)
//   XMT  → 3.3V (unmute)
//
// Vezérlés: 2 nyomógomb encoder helyett.
//   BOOT gomb (GPIO0):  rövid = EV_PRESS, hosszú = EV_LONG
//   KEY  gomb (GPIO14): rövid = EV_CW (következő), hosszú = EV_CCW (előző)
// ─────────────────────────────────────────────────────────────────────────────
#define SMART_WEBRADIO_DISPLAY_320X240 1  // LILYGO T-Display S3: 320x170, uses the 320-wide UI path

#define UI_SCREEN_W 320
#define UI_SCREEN_H 170
#define SMART_WEBRADIO_ENABLE_ENCODER 1
#define SMART_WEBRADIO_DISPLAY_INVERT false

// PCM5102A I2S – valóban szabad GPIO-k
#define I2S_DOUT  1
#define I2S_BCLK  2
#define I2S_LRCK  3

// TFT makrók – csak kompatibilitás miatt, az LGFX nem SPI-t használ
#define TFT_BL    38
#define TFT_RST   5

// Gombok – enkóder helyett
#define BTN_BOOT  0    // BOOT gomb: EV_PRESS / EV_LONG
#define BTN_KEY   14   // KEY  gomb: EV_CW    / EV_CCW
#define BACKLIGHT_FULL      200 //  0-255
#define BACKLIGHT_DIM       50  //  0-255
#define BACKLIGHT_DIM_AFTER 60000UL  // 1 minutes


/* ESP32S3 SUPERMINI */

/*ENC_CLK  (pin 11)  =  BTN_NEXT  →  EV_CW  (hangerő fel / következő)
ENC_DT   (pin 10)  =  BTN_PREV  →  EV_CCW (hangerő le / előző)
ENC_SW   (pin  6)  =  BTN_OK    →  EV_PRESS / EV_LONG	*/

#define RB_HOST "http://de1.api.radio-browser.info"   // API marad HTTP-n: ESP32-n stabilabb, mint TLS/HTTPS
#define RB_PAGE_SIZE 30
#define RB_API_LIMIT RB_PAGE_SIZE
#define RB_RESULT_LIMIT 35
#define VOL_MAX 21
#define INITIAL_VOLUME 8
#define SERIAL_BAUDRATE 460800

#define STATIONS_FILE "/stations.txt"
#define WIFI_CONFIG_FILE "/wifi.json"
#define HTTP_SERVER_PORT 80

// ── Editable country list — add/remove as needed ──────────
// Format: {"CODE", "Display Name"}
// Codes must match Radio Browser countrycode field (ISO 3166-1 alpha-2)
struct CountryEntry {
  const char* code;
  const char* name;
};
static const CountryEntry COUNTRIES[] = {
  { "all", "All" },
  { "local", "Local" },
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
static const int COUNTRY_COUNT = sizeof(COUNTRIES) / sizeof(COUNTRIES[0]);

// ── Editable genre list ────────────────────────────────────
struct GenreEntry {
  const char* tag;
  const char* label;
};
static const GenreEntry GENRES[] = {
  { "all", "All" },
  { "local", "Local" },
  { "music", "Music" },
  { "news", "News" },
  { "jazz", "Jazz" },
  { "classical", "Classical" },
  { "rock", "Rock" },
};
static const int GENRE_COUNT = sizeof(GENRES) / sizeof(GENRES[0]);

// ── LovyanGFX display driver – T-Display S3 8-bites párhuzamos busz ──────────
// A T-Display S3 ST7789V panelt 8-bites párhuzamos interfészen hajtja,
// NEM SPI-n! Bus_Parallel8 szükséges.
// Háttérvilágítás: GPIO38 (közvetlen digitalwrite, nem PWM csatorna).
// Tápkapcsoló: GPIO15 HIGH = bekapcsolt állapot.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789   _panel_instance;
  lgfx::Bus_Parallel8  _bus_instance;

public:
  LGFX(void) {
    // 8-bites párhuzamos busz konfiguráció
    auto bus_cfg = _bus_instance.config();
    bus_cfg.port            = 0;
    bus_cfg.freq_write      = 20000000;
    bus_cfg.pin_wr          = 8;
    bus_cfg.pin_rd          = 9;
    bus_cfg.pin_rs          = 7;   // DC (Data/Command)
    bus_cfg.pin_d0          = 39;
    bus_cfg.pin_d1          = 40;
    bus_cfg.pin_d2          = 41;
    bus_cfg.pin_d3          = 42;
    bus_cfg.pin_d4          = 45;
    bus_cfg.pin_d5          = 46;
    bus_cfg.pin_d6          = 47;
    bus_cfg.pin_d7          = 48;
    _bus_instance.config(bus_cfg);
    _panel_instance.setBus(&_bus_instance);

    // Panel konfiguráció
    auto panel_cfg = _panel_instance.config();
    panel_cfg.pin_cs           = 6;
    panel_cfg.pin_rst          = 5;
    panel_cfg.pin_busy         = -1;
    panel_cfg.memory_width     = 170;
    panel_cfg.memory_height    = 320;
    panel_cfg.panel_width      = 170;
    panel_cfg.panel_height     = 320;
    panel_cfg.offset_x         = 35;
    panel_cfg.offset_y         = 0;
    panel_cfg.offset_rotation  = 0;
    panel_cfg.invert           = true;
    panel_cfg.rgb_order        = false;
    panel_cfg.dummy_read_pixel = 8;
    panel_cfg.dummy_read_bits  = 1;
    panel_cfg.readable         = false;
    panel_cfg.dlen_16bit       = false;
    panel_cfg.bus_shared       = false;
    _panel_instance.config(panel_cfg);

    setPanel(&_panel_instance);
  }
};

#define ST77XX_BLACK   TFT_BLACK
#define ST77XX_WHITE   TFT_WHITE
#define ST77XX_RED     TFT_RED
#define ST77XX_GREEN   TFT_GREEN
#define ST77XX_BLUE    TFT_BLUE
#define ST77XX_CYAN    TFT_CYAN
#define ST77XX_MAGENTA TFT_MAGENTA
#define ST77XX_YELLOW  TFT_YELLOW

Audio audio;
Preferences prefs;
LGFX tft;
LGFX_Sprite canvas(&tft);
// Small off-screen line buffer for clipped metadata text.
// This prevents long scrolled artist/title text from wrapping or bleeding
// outside the intended 144 px text area on the ST7735.
LGFX_Sprite infoLineCanvas(&tft);
LGFX_Sprite stationSelectCanvas(&tft);
QueueHandle_t encQueue;
WebServer server(80);

enum EncEvent { EV_CW,
                EV_CCW,
                EV_PRESS,
                EV_LONG };
enum UiMode { MODE_NORMAL,
              MODE_BROWSE,
              MODE_EDIT };
enum FocusItem { F_NOWPLAYING,
                 F_COUNTRY,
                 F_TYPE };

struct Station {
  String name;
  String url;
  int bitrate;
  String codec;
  String country;
};

Station stations[RB_RESULT_LIMIT];
int stationCount = 0;
int currentStation = 0;
int focusIndex = 0;
UiMode uiMode = MODE_NORMAL;
bool uiDirty = true;
String streamTitle = "Loading...";
String metaArtist = "";
String metaTitle = "";
bool metaAvailable = false;
String lastMetaRaw = "";
int previewStation = 0;
String previewTag = "all";
String previewCountry = "HU";
String searchTag = "all";
String selectedGenre = "all";
String selectedCountry = "HU";
int rbOffset = 0;       // Radio Browser pagination offset
int rbPage = 0;         // 0-based page number shown as page + 1
bool rbLastPageShort = false;
int bitrateCap = 0;   // 0 = nincs bitrate szures

String lastStationUrl = "";
String lastStationName = "";
String lastStationCountry = "HU";
int lastStationOffset = 0;
bool restoreLastStationPending = false;

bool muted = false;
int lastVol = INITIAL_VOLUME;
uint32_t browseLastAction = 0;
uint32_t lastUserActionMs = 0;
bool backlightDimmed = false;
bool usingLocalStations = false;
String deviceIpText = "No IP";
String activeWifiSsid = "";
String activeWifiPassword = "";
bool activeWifiFromSPIFFS = false;
bool wifiSetupModeActive = false;
String currentCodecName = "";
int currentBitrateKbps = 0;
uint32_t lastAudioStatusPollMs = 0;

// Station selector text scroll: long names run through once, wait 5 seconds, then repeat.
static const uint32_t STATION_SCROLL_PAUSE_MS = 5000UL;
static const uint32_t STATION_SCROLL_STEP_MS  = 45UL;
int scrollLastStation = -1;
String scrollLastText = "";
uint32_t scrollStartMs = 0;
uint32_t lastScrollFrameMs = 0;

// Normal screen info scroll: station / artist / title.
// Only one line may scroll at a time to keep redraw work low.
static const uint32_t INFO_SCROLL_PAUSE_MS = 5000UL;
static const uint32_t INFO_SCROLL_STEP_MS  = 45UL;
static const int INFO_LINE_STATION = 0;
static const int INFO_LINE_ARTIST  = 1;
static const int INFO_LINE_TITLE   = 2;
int infoScrollLine = -1;
String infoScrollKey = "";
uint32_t infoScrollStartMs = 0;

volatile uint32_t holdStartMs = 0;
volatile bool buttonHolding = false;

// ---- Serial MRSPIFS maintenance protocol for Partition Manager ----
static const size_t MRSPIFS_MAX_LINE = 98304;
void handleSerialMaintenance();
void serviceMaintenanceFor(uint32_t ms);
String normalizeFsPath(String path);

// Newer ESP32-audioI2S versions use this central callback instead of only
// the older global audio_show... callback functions.
void my_audio_info(Audio::msg_t m);

String cleanStreamUrl(String u) {
  u.trim();
  bool hadSemicolon = false;
  while (u.endsWith(";")) {
    u.remove(u.length() - 1);
    hadSemicolon = true;
  }
  if (hadSemicolon && u.endsWith("/")) {
    int lastDot = u.lastIndexOf('.');
    int lastSlash = u.lastIndexOf('/');
    if (lastDot > 0 && lastDot < lastSlash) u.remove(u.length() - 1);
  }
  return u;
}

bool supportedCodec(const String& codec, const String& u) {
  String c = codec;
  c.toUpperCase();
  String lu = u;
  lu.toLowerCase();
  if (c.indexOf("MP3") >= 0) return true;
  if (c.indexOf("AAC") >= 0) return true;
  if (c.indexOf("FLAC") >= 0) return true;
  if (c.indexOf("OPUS") >= 0) return true;
  if (c.indexOf("VORBIS") >= 0 || c.indexOf("OGG") >= 0) return true;
  if (lu.indexOf(".mp3") >= 0) return true;
  if (lu.indexOf("aac") >= 0) return true;
  if (lu.indexOf("flac") >= 0) return true;
  if (lu.indexOf("opus") >= 0) return true;
  if (lu.indexOf("ogg") >= 0) return true;
  return false;
}

String normalizeCodecName(String codec) {
  codec.trim();
  if (codec.length() == 0) return "";

  String c = codec;
  c.toUpperCase();

  // Ignore non-codec diagnostic text that the audio library may emit.
  if (c == "UNKNOWN" || c == "UNDEFINED" || c == "NONE" || c == "NULL") return "";
  if (c.indexOf("NO CODEC") >= 0) return "";

  // HTTP content-type / ICY / decoder wording variants.
  // Examples: audio/mpeg, audio/aacp, audio/aac, MPEG-1 Layer 3, mp4a.
  if (c.indexOf("AACP") >= 0 || c.indexOf("AAC+") >= 0 || c.indexOf("HE-AAC") >= 0 || c.indexOf("HEAAC") >= 0) return "AAC+";
  if (c.indexOf("AAC") >= 0 || c.indexOf("MP4A") >= 0 || c.indexOf("M4A") >= 0 || c.indexOf("AUDIO/MP4") >= 0) return "AAC";
  if (c.indexOf("MP3") >= 0 || c.indexOf("MPEG") >= 0 || c.indexOf("MPGA") >= 0 || c.indexOf("AUDIO/MPA") >= 0) return "MP3";
  if (c.indexOf("OPUS") >= 0) return "OPUS";
  if (c.indexOf("VORBIS") >= 0 || c.indexOf("OGG") >= 0) return "OGG";
  if (c.indexOf("FLAC") >= 0) return "FLAC";
  if (c.indexOf("WAV") >= 0 || c.indexOf("PCM") >= 0) return "WAV";

  // If the message is just a short codec-like token, show it; otherwise ignore it.
  if (c.length() <= 6 && c.indexOf(' ') < 0 && c.indexOf(':') < 0 && c.indexOf('/') < 0) return c;
  return "";
}

String guessCodecFromUrl(String url) {
  String u = url;
  u.toLowerCase();
  if (u.indexOf("mp3") >= 0) return "MP3";
  if (u.indexOf("aacp") >= 0 || u.indexOf("aac+") >= 0) return "AAC+";
  if (u.indexOf("aac") >= 0 || u.indexOf("m4a") >= 0) return "AAC";
  if (u.indexOf("opus") >= 0) return "OPUS";
  if (u.indexOf("ogg") >= 0) return "OGG";
  if (u.indexOf("flac") >= 0) return "FLAC";
  return "";
}

int parseFirstNumber(String s) {
  String digits = "";
  for (int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') digits += c;
    else if (digits.length() > 0) break;
  }
  if (digits.length() == 0) return 0;
  return digits.toInt();
}

int parseNumberAfterKey(const String& src, const String& key) {
  String lower = src;
  lower.toLowerCase();
  String k = key;
  k.toLowerCase();
  int pos = lower.indexOf(k);
  if (pos < 0) return 0;
  pos += k.length();
  while (pos < src.length() && !(src[pos] >= '0' && src[pos] <= '9')) pos++;
  String digits = "";
  while (pos < src.length() && src[pos] >= '0' && src[pos] <= '9') {
    digits += src[pos++];
  }
  return digits.length() ? digits.toInt() : 0;
}

void updateBitrateFromText(String msg) {
  String lower = msg;
  lower.toLowerCase();

  int n = 0;

  // Explicit bitrate messages, for example: "bitrate: 1411200", "1411 kbps".
  if (lower.indexOf("bitrate") >= 0 || lower.indexOf("bit rate") >= 0 || lower.indexOf("kbps") >= 0 || lower.indexOf("kbit") >= 0) {
    n = parseFirstNumber(msg);
    // Some callbacks report bits/sec, some report kbit/sec.
    if (n > 10000) n = (n + 500) / 1000;
  }

  // FLAC/PCM info often arrives as sampleRate + bitsPerSample + channels,
  // but without a direct bitrate event. Example: 44100 * 16 * 2 = 1411 kbps.
  if (n <= 0 && (lower.indexOf("samplerate") >= 0 || lower.indexOf("sample rate") >= 0)) {
    int sr = parseNumberAfterKey(msg, "sampleRate");
    if (sr <= 0) sr = parseNumberAfterKey(msg, "sample rate");
    int bits = parseNumberAfterKey(msg, "bitsPerSample");
    if (bits <= 0) bits = parseNumberAfterKey(msg, "bits per sample");
    int ch = parseNumberAfterKey(msg, "channels");
    if (sr > 0 && bits > 0 && ch > 0) {
      n = (int)(((uint64_t)sr * (uint64_t)bits * (uint64_t)ch + 500ULL) / 1000ULL);
    }
  }

  if (n > 0 && n < 10000 && n != currentBitrateKbps) {
    currentBitrateKbps = n;
    uiDirty = true;
  }
}

void updateCodecFromText(String msg) {
  String codec = normalizeCodecName(msg);
  if (codec.length() > 0 && codec != currentCodecName) {
    currentCodecName = codec;
    uiDirty = true;
  }
}

void refreshRuntimeAudioStatus() {
  // Poll from loop(), not from the audio callback. The new callback API warns that
  // event callbacks should not directly call Audio object methods.
  if (millis() - lastAudioStatusPollMs < 1000UL) return;
  lastAudioStatusPollMs = millis();

  uint32_t br = audio.getBitRate();
  int brK = 0;
  if (br > 0) {
    brK = (br > 2000) ? (int)((br + 500) / 1000) : (int)br;
    if (brK > 0 && brK < 10000 && brK != currentBitrateKbps) {
      currentBitrateKbps = brK;
      uiDirty = true;
    }
  }

  String codec = normalizeCodecName(String(audio.getCodecname() ? audio.getCodecname() : ""));
  if (codec.length() == 0 && stationCount > 0 && currentStation >= 0 && currentStation < stationCount) {
    codec = normalizeCodecName(stations[currentStation].codec);
    if (codec.length() == 0) codec = guessCodecFromUrl(stations[currentStation].url);
  }

  if (codec.length() > 0 && codec != currentCodecName) {
    currentCodecName = codec;
    Serial.print("[AUDIO codec poll] ");
    Serial.println(currentCodecName);
    uiDirty = true;
  }
}

String bitrateLabel() {
  if (currentBitrateKbps <= 0) return "--k";
  if (currentBitrateKbps >= 1000) {
    int tenths = (currentBitrateKbps + 50) / 100;  // 1411 -> 14 -> 1.4M
    return String(tenths / 10) + "." + String(tenths % 10) + "M";
  }
  return String(currentBitrateKbps) + "k";
}

String audioStatusLabel(bool compact = false) {
  String br = bitrateLabel();
  String co = currentCodecName.length() ? currentCodecName : "---";
  if (compact) return br;
  return br + " " + co;
}

void appendUtf8(String& out, uint32_t cp) {
  if (cp <= 0x7F) {
    out += (char)cp;
  } else if (cp <= 0x7FF) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

bool isValidUtf8(const String& s) {
  int i = 0;
  while (i < s.length()) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) {
      i++;
    } else if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= s.length()) return false;
      uint8_t c1 = (uint8_t)s[i + 1];
      if ((c1 & 0xC0) != 0x80 || c < 0xC2) return false;
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= s.length()) return false;
      uint8_t c1 = (uint8_t)s[i + 1];
      uint8_t c2 = (uint8_t)s[i + 2];
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
      i += 3;
    } else if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= s.length()) return false;
      uint8_t c1 = (uint8_t)s[i + 1];
      uint8_t c2 = (uint8_t)s[i + 2];
      uint8_t c3 = (uint8_t)s[i + 3];
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

uint32_t cp1250ToUnicode(uint8_t c) {
  switch (c) {
    case 0x80: return 0x20AC;
    case 0x82: return 0x201A;
    case 0x84: return 0x201E;
    case 0x85: return 0x2026;
    case 0x86: return 0x2020;
    case 0x87: return 0x2021;
    case 0x89: return 0x2030;
    case 0x8A: return 0x0160;
    case 0x8B: return 0x2039;
    case 0x8C: return 0x015A;
    case 0x8D: return 0x0164;
    case 0x8E: return 0x017D;
    case 0x8F: return 0x0179;
    case 0x91: return 0x2018;
    case 0x92: return 0x2019;
    case 0x93: return 0x201C;
    case 0x94: return 0x201D;
    case 0x95: return 0x2022;
    case 0x96: return 0x2013;
    case 0x97: return 0x2014;
    case 0x99: return 0x2122;
    case 0x9A: return 0x0161;
    case 0x9B: return 0x203A;
    case 0x9C: return 0x015B;
    case 0x9D: return 0x0165;
    case 0x9E: return 0x017E;
    case 0x9F: return 0x017A;
    case 0xA1: return 0x02C7;
    case 0xA2: return 0x02D8;
    case 0xA3: return 0x0141;
    case 0xA5: return 0x0104;
    case 0xAA: return 0x015E;
    case 0xAF: return 0x017B;
    case 0xB2: return 0x02DB;
    case 0xB3: return 0x0142;
    case 0xB9: return 0x0105;
    case 0xBA: return 0x015F;
    case 0xBC: return 0x013D;
    case 0xBD: return 0x02DD;
    case 0xBE: return 0x013E;
    case 0xBF: return 0x017C;
    case 0xC0: return 0x0154;
    case 0xC3: return 0x0102;
    case 0xC5: return 0x0139;
    case 0xC6: return 0x0106;
    case 0xC8: return 0x010C;
    case 0xCA: return 0x0118;
    case 0xCC: return 0x011A;
    case 0xCF: return 0x010E;
    case 0xD0: return 0x0110;
    case 0xD1: return 0x0143;
    case 0xD2: return 0x0147;
    case 0xD5: return 0x0150;
    case 0xD8: return 0x0158;
    case 0xD9: return 0x016E;
    case 0xDB: return 0x0170;
    case 0xDE: return 0x0162;
    case 0xE0: return 0x0155;
    case 0xE3: return 0x0103;
    case 0xE5: return 0x013A;
    case 0xE6: return 0x0107;
    case 0xE8: return 0x010D;
    case 0xEA: return 0x0119;
    case 0xEC: return 0x011B;
    case 0xEF: return 0x010F;
    case 0xF0: return 0x0111;
    case 0xF1: return 0x0144;
    case 0xF2: return 0x0148;
    case 0xF5: return 0x0151;
    case 0xF8: return 0x0159;
    case 0xF9: return 0x016F;
    case 0xFB: return 0x0171;
    case 0xFE: return 0x0163;
    default: return c;
  }
}

uint32_t latin1ToUnicode(uint8_t c) {
  switch (c) {
    case 0x80: return 0x20AC;
    case 0x82: return 0x201A;
    case 0x84: return 0x201E;
    case 0x85: return 0x2026;
    case 0x86: return 0x2020;
    case 0x87: return 0x2021;
    case 0x89: return 0x2030;
    case 0x8B: return 0x2039;
    case 0x91: return 0x2018;
    case 0x92: return 0x2019;
    case 0x93: return 0x201C;
    case 0x94: return 0x201D;
    case 0x95: return 0x2022;
    case 0x96: return 0x2013;
    case 0x97: return 0x2014;
    case 0x99: return 0x2122;
    case 0x9B: return 0x203A;
    default: return c;
  }
}

bool looksLikeCp1250(const String& s) {
  for (int i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    switch (c) {
      case 0x8A: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
      case 0x9A: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
      case 0xD5: case 0xDB: case 0xF5: case 0xFB:
        return true;
    }
  }
  return false;
}
String singleByteTextToUtf8(const String& s) {
  bool cp1250 = looksLikeCp1250(s);
  String out;
  out.reserve(s.length() * 2);
  for (int i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) out += (char)c;
    else appendUtf8(out, cp1250 ? cp1250ToUnicode(c) : latin1ToUnicode(c));
  }
  return out;
}

String asciiText(String s) {
  // Historical name kept to avoid a large refactor. It now returns display text.
  // Correct UTF-8 is preserved; single-byte metadata is upgraded to UTF-8.
  if (!isValidUtf8(s)) s = singleByteTextToUtf8(s);

  s.replace("&amp;", "&");
  s.replace("&quot;", "\"");
  s.replace("&#39;", "'");
  s.replace("&apos;", "'");
  s.replace("&nbsp;", " ");

  String out;
  out.reserve(s.length());
  for (int i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c >= 32 || c >= 128) out += (char)c;
  }
  out.trim();
  if (out.length() == 0) out = "Unknown";
  return out;
}

bool parseStationLine(String line, String& name, String& url) {
  line.trim();
  if (line.length() == 0) return false;
  if (line.startsWith("#")) return false;

  int sep = line.indexOf('\t');
  if (sep < 0) sep = line.indexOf(';');
  if (sep < 0) sep = line.indexOf(',');
  if (sep < 0) return false;

  name = line.substring(0, sep);
  String rest = line.substring(sep + 1);
  int sep2 = rest.indexOf('\t');
  if (sep2 < 0) sep2 = rest.indexOf(';');
  if (sep2 < 0) sep2 = rest.indexOf(',');
  url = (sep2 >= 0) ? rest.substring(0, sep2) : rest;

  name.trim();
  url.trim();
  url = cleanStreamUrl(url);
  if (name.length() == 0 || url.length() == 0) return false;
  if (!(url.startsWith("http://") || url.startsWith("https://"))) return false;
  return true;
}

bool loadStationsFromSPIFFS(int newOffset = -1) {
  if (newOffset >= 0) rbOffset = max(0, newOffset);

  if (!SPIFFS.exists(STATIONS_FILE)) {
    Serial.println("SPIFFS stations.txt not found.");
    return false;
  }

  File f = SPIFFS.open(STATIONS_FILE, FILE_READ);
  if (!f) {
    Serial.println("Could not open /stations.txt from SPIFFS.");
    return false;
  }

  stationCount = 0;
  int validIndex = 0;
  bool hasMoreAfterPage = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    String name, url;
    if (!parseStationLine(line, name, url)) continue;

    // Skip valid stations before the requested local page offset.
    if (validIndex < rbOffset) {
      validIndex++;
      continue;
    }

    // Local SPIFFS uses the same visible page size as Radio Browser.
    // The stations[] buffer is slightly larger, but the UI page should stay 30 entries.
    if (stationCount >= RB_PAGE_SIZE) {
      hasMoreAfterPage = true;
      break;
    }

    stations[stationCount].name = asciiText(name);
    stations[stationCount].url = url;
    stations[stationCount].bitrate = 0;
    stations[stationCount].codec = guessCodecFromUrl(url);
    stations[stationCount].country = "LOCAL";

    Serial.print("SPIFFS +");
    Serial.print(stationCount + 1);
    Serial.print(" ");
    Serial.print(stations[stationCount].name);
    Serial.print(" | ");
    Serial.println(stations[stationCount].url);
    stationCount++;
    validIndex++;
  }
  f.close();

  usingLocalStations = (stationCount > 0);
  if (!usingLocalStations) {
    Serial.println("/stations.txt found, but no playable stations were parsed on this page.");
    return false;
  }

  selectedCountry = "local";
  // Keep the current genre independent from the Local country source.
  // This lets the user switch back from Country=Local to any online country
  // without Genre=Local forcing SPIFFS again.
  if (selectedGenre == "local") selectedGenre = "all";
  previewCountry = selectedCountry;
  previewTag = selectedGenre;
  rbPage = rbOffset / RB_PAGE_SIZE;
  rbLastPageShort = !hasMoreAfterPage;

  Serial.print("Loaded local SPIFFS stations: ");
  Serial.print(stationCount);
  Serial.print(" | page: ");
  Serial.println(rbPage + 1);
  return true;
}


String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

bool loadWifiConfigFromSPIFFS() {
  activeWifiSsid = "";
  activeWifiPassword = "";
  activeWifiFromSPIFFS = false;

  if (!SPIFFS.exists(WIFI_CONFIG_FILE)) {
    Serial.println("/wifi.json not found. WiFi setup AP will be started.");
    return false;
  }

  File f = SPIFFS.open(WIFI_CONFIG_FILE, FILE_READ);
  if (!f) {
    Serial.println("Could not open /wifi.json. WiFi setup AP will be started.");
    return false;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.print("Invalid /wifi.json: ");
    Serial.println(err.c_str());
    Serial.println("WiFi setup AP will be started.");
    return false;
  }

  String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  ssid.trim();

  if (ssid.length() == 0) {
    Serial.println("/wifi.json has empty ssid. WiFi setup AP will be started.");
    return false;
  }

  activeWifiSsid = ssid;
  activeWifiPassword = password;
  activeWifiFromSPIFFS = true;
  Serial.print("WiFi config loaded from /wifi.json: ");
  Serial.println(activeWifiSsid);
  return true;
}

bool saveWifiConfigToSPIFFS(const String& ssidIn, const String& passwordIn) {
  String ssid = ssidIn;
  String password = passwordIn;
  ssid.trim();

  if (ssid.length() == 0) return false;

  DynamicJsonDocument doc(512);
  doc["ssid"] = ssid;
  doc["password"] = password;

  File f = SPIFFS.open(WIFI_CONFIG_FILE, FILE_WRITE);
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.println();
  f.close();

  activeWifiSsid = ssid;
  activeWifiPassword = password;
  activeWifiFromSPIFFS = true;
  return true;
}

void startWifiSetupAP() {
  Serial.println("Starting setup AP: SmartWebRadio-Setup");
  wifiSetupModeActive = true;
  activeWifiFromSPIFFS = false;
  activeWifiSsid = "";

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.disconnect(true, true);
  delay(300);
  WiFi.mode(WIFI_OFF);
  delay(500);

  WiFi.mode(WIFI_AP);
  delay(200);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  bool cfgOk = WiFi.softAPConfig(apIP, gateway, subnet);

  bool apOk = WiFi.softAP("SmartWebRadio-Setup", nullptr, 1, false, 4);
  delay(700);

  // Retry once in AP+STA mode. Some ESP32-S3 core builds are more reliable this way after a failed STA attempt.
  if (!apOk || !(WiFi.getMode() & WIFI_AP)) {
    Serial.println("Setup AP first attempt failed, retrying in AP_STA mode...");
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_AP_STA);
    delay(200);
    WiFi.softAPConfig(apIP, gateway, subnet);
    apOk = WiFi.softAP("SmartWebRadio-Setup", nullptr, 1, false, 4);
    delay(700);
  }

  deviceIpText = WiFi.softAPIP().toString();
  streamTitle = apOk ? "Setup AP: SmartWebRadio-Setup" : "Setup AP failed";
  Serial.print("Setup AP config: ");
  Serial.println(cfgOk ? "OK" : "FAILED");
  Serial.print("Setup AP status: ");
  Serial.println(apOk ? "OK" : "FAILED");
  Serial.print("Setup AP SSID: ");
  Serial.println("SmartWebRadio-Setup");
  Serial.print("Setup AP IP: ");
  Serial.println(deviceIpText);
  Serial.print("WiFi mode: ");
  Serial.println((int)WiFi.getMode());
}


void connectConfiguredWiFi(uint32_t timeoutMs = 15000UL) {
  if (!loadWifiConfigFromSPIFFS()) {
    startWifiSetupAP();
    return;
  }

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(activeWifiSsid.c_str(), activeWifiPassword.c_str());

  Serial.print("Connecting WiFi SSID: ");
  Serial.print(activeWifiSsid);
  Serial.println(" (/wifi.json)");

  uint32_t wifiStartMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartMs < timeoutMs) {
    serviceMaintenanceFor(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiSetupModeActive = false;
    deviceIpText = WiFi.localIP().toString();
    Serial.print("WiFi IP: ");
    Serial.println(deviceIpText);
    return;
  }

  Serial.println("WiFi timeout.");
  startWifiSetupAP();
}

void handleGetWifi() {
  DynamicJsonDocument doc(512);
  doc["ssid"] = activeWifiSsid;
  doc["source"] = activeWifiFromSPIFFS ? "SPIFFS /wifi.json" : "setup AP / no valid wifi.json";
  doc["ip"] = deviceIpText;
  doc["mode"] = wifiSetupModeActive ? "AP setup" : ((WiFi.getMode() & WIFI_AP) ? "AP" : "STA");
  String out;
  serializeJsonPretty(doc, out);
  server.send(200, "application/json", out);
}

void handlePostWifi() {
  String ssid;
  String password;

  String body = server.arg("plain");
  if (body.length() > 0 && body[0] == '{') {
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      server.send(400, "text/plain", "Invalid JSON. Use {\"ssid\":\"...\",\"password\":\"...\"}.\n");
      return;
    }
    ssid = doc["ssid"] | "";
    password = doc["password"] | "";
  } else {
    ssid = server.arg("ssid");
    password = server.arg("password");
  }

  ssid.trim();
  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID is required.\n");
    return;
  }

  if (!saveWifiConfigToSPIFFS(ssid, password)) {
    server.send(500, "text/plain", "Could not write /wifi.json.\n");
    return;
  }

  String msg = "Saved /wifi.json. Restart the radio, or power-cycle it, to connect with the new WiFi settings.\n";
  msg += "SSID: " + ssid + "\n";
  server.send(200, "text/plain", msg);
}

void handleRoot() {
  String html = "<!doctype html><html><head><meta charset='utf-8'><title>Smart WebRadio</title>";
  html += "<style>body{font-family:Arial;margin:24px;background:#111;color:#eee}textarea{width:100%;height:45vh}input{padding:8px;margin:4px 0;width:320px;max-width:100%;box-sizing:border-box}button{padding:10px 16px}a{color:#7df}.card{border:1px solid #333;padding:14px;margin:14px 0;background:#181818}</style></head><body>";
  html += "<h2>Smart WebRadio SPIFFS test</h2>";
  html += "<p>IP: <b>" + deviceIpText + "</b></p>";
  html += "<div class='card'><h3>WiFi settings</h3>";
  html += "<p>Source: <b>" + String(activeWifiFromSPIFFS ? "SPIFFS /wifi.json" : "setup AP / no valid wifi.json") + "</b></p>";
  html += "<form method='POST' action='/api/wifi'>";
  html += "<label>SSID<br><input name='ssid' value='" + htmlEscape(activeWifiSsid) + "'></label><br>";
  html += "<label>Password<br><input name='password' type='password' value='" + htmlEscape(activeWifiPassword) + "'></label><br><br>";
  html += "<button type='submit'>Save /wifi.json</button>";
  html += "</form><p>After saving, restart/power-cycle the radio.</p></div>";
  html += "<p><a href='/api/stations'>Download /stations.txt</a> | <a href='/api/wifi'>View WiFi JSON status</a></p>";
  html += "<form method='POST' action='/api/stations'><textarea name='stations'>";
  if (SPIFFS.exists(STATIONS_FILE)) {
    File f = SPIFFS.open(STATIONS_FILE, FILE_READ);
    while (f && f.available()) html += (char)f.read();
    if (f) f.close();
  } else {
    html += "Danubius Radio\thttps://danubiusradio.hu/live_HiFi.mp3\n";
  }
  html += "</textarea><br><br><button type='submit'>Save stations.txt</button></form>";
  html += "<p>Format: Station name &lt;TAB&gt; stream URL. A 3rd myRadio logo column is ignored.</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleGetStations() {
  if (!SPIFFS.exists(STATIONS_FILE)) {
    server.send(404, "text/plain", "No /stations.txt on SPIFFS yet.\n");
    return;
  }
  File f = SPIFFS.open(STATIONS_FILE, FILE_READ);
  server.streamFile(f, "text/plain");
  f.close();
}

void handlePostStations() {
  String body = server.arg("plain");
  if (body.length() == 0 && server.hasArg("stations")) body = server.arg("stations");
  if (body.length() == 0) {
    server.send(400, "text/plain", "Empty stations body. POST raw text or form field named stations.\n");
    return;
  }

  File f = SPIFFS.open(STATIONS_FILE, FILE_WRITE);
  if (!f) {
    server.send(500, "text/plain", "Could not write /stations.txt.\n");
    return;
  }
  f.print(body);
  f.close();

  bool ok = loadStationsFromSPIFFS();
  if (ok) {
    previewStation = 0;
    playStation(0);
  }
  uiDirty = true;
  server.send(200, "text/plain", ok ? "Saved and loaded /stations.txt.\n" : "Saved, but no valid stations parsed.\n");
}

void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/stations", HTTP_GET, handleGetStations);
  server.on("/api/stations", HTTP_POST, handlePostStations);
  server.on("/api/wifi", HTTP_GET, handleGetWifi);
  server.on("/api/wifi", HTTP_POST, handlePostWifi);
  server.on("/api/ip", HTTP_GET, []() {
    server.send(200, "text/plain", deviceIpText + "\n");
  });
  server.begin(HTTP_SERVER_PORT);
  Serial.print("HTTP server: http://");
  Serial.println(deviceIpText);
}

// ---------------- Serial MRSPIFS protocol for LittleFS-SPIFFS Partition Manager ----------------
File mrspifsWriteFile;
String mrspifsWritePath;
size_t mrspifsExpectedSize = 0;
size_t mrspifsWrittenSize = 0;
bool mrspifsWriting = false;
bool mrspifsLineOverflow = false;
uint32_t mrspifsLastActivityMs = 0;

// Robust serial-only maintenance boot.
// Partition Manager upload must not run while WiFi/audio/UI is active,
// otherwise large VLW writes can starve/reset the ESP32-S3 or pollute Serial.
RTC_DATA_ATTR uint32_t mrspifsBootMagic = 0;
static const uint32_t MRSPIFS_BOOT_MAGIC = 0x4D525350UL;  // "MRSP"
bool serialMaintenanceOnly = false;
void enterSerialMaintenanceMode();

String normalizeFsPath(String path) {
  path.trim();
  path.replace("\\", "/");
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  if (!path.startsWith("/")) path = "/" + path;
  if (path.length() == 0) path = "/";
  return path;
}

void mrspifsSend(const String& msg) {
  Serial.print("MRSPIFS|");
  Serial.println(msg);
}

String spiffsUsageText() {
  return String(SPIFFS.usedBytes()) + "/" + String(SPIFFS.totalBytes());
}

void mrspifsListDir(const char* dirname) {
  File root = SPIFFS.open(dirname);
  if (!root || !root.isDirectory()) {
    mrspifsSend("ERR|LIST|open_failed");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    String path = file.name();
    if (!path.startsWith("/")) path = "/" + path;
    if (file.isDirectory()) {
      mrspifsSend("DIR|" + path);
    } else {
      mrspifsSend("FILE|" + path + "|" + String((uint32_t)file.size()));
    }
    file = root.openNextFile();
    yield();
  }
  mrspifsSend("OK|LIST");
}

int mrspifsRemoveTree(const String& path) {
  int removed = 0;
  const int MAX_DELETE_BATCH = 96;
  String targets[MAX_DELETE_BATCH];
  int targetCount = 0;

  if (path == "/") {
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file && targetCount < MAX_DELETE_BATCH) {
      String child = file.name();
      if (!child.startsWith("/")) child = "/" + child;
      targets[targetCount++] = child;
      file.close();
      file = root.openNextFile();
      yield();
    }
    root.close();
    for (int i = 0; i < targetCount; i++) {
      if (SPIFFS.remove(targets[i])) removed++;
      yield();
    }
    return removed;
  }

  if (SPIFFS.exists(path) && SPIFFS.remove(path)) removed++;

  String prefix = path;
  if (!prefix.endsWith("/")) prefix += "/";
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file && targetCount < MAX_DELETE_BATCH) {
    String child = file.name();
    if (!child.startsWith("/")) child = "/" + child;
    file.close();
    if (child.startsWith(prefix)) targets[targetCount++] = child;
    file = root.openNextFile();
    yield();
  }
  root.close();
  for (int i = 0; i < targetCount; i++) {
    if (SPIFFS.remove(targets[i])) removed++;
    yield();
  }
  return removed;
}

String mrspifsAlternatePath(const String& path) {
  if (path.startsWith("/fonts/")) return "/" + path.substring(7);
  if (path.indexOf('/', 1) < 0) return "/fonts" + path;
  return "";
}

int mrspifsRemovePathWithAliases(const String& path) {
  int removed = mrspifsRemoveTree(path);
  if (removed > 0) return removed;

  String alternate = mrspifsAlternatePath(path);
  if (alternate.length()) removed = mrspifsRemoveTree(alternate);
  return removed;
}

void mrspifsAbortWrite(bool removePartial = true) {
  if (mrspifsWriteFile) mrspifsWriteFile.close();
  if (removePartial && mrspifsWriting && mrspifsWritePath.length()) SPIFFS.remove(mrspifsWritePath);
  mrspifsWriting = false;
  mrspifsWritePath = "";
  mrspifsExpectedSize = 0;
  mrspifsWrittenSize = 0;
  mrspifsLineOverflow = false;
  mrspifsLastActivityMs = millis();
}

void handleSerialMaintenanceLine(String line) {
  line.trim();
  if (!line.length()) return;

  if (line == "REBOOT_MAINT" || line == "MAINT_REBOOT") {
    // The desktop Partition Manager can lose the native USB CDC port while the
    // ESP32-S3 re-enumerates after ESP.restart().  Enter maintenance mode in-place
    // instead: stop radio services, keep the same COM port open, then acknowledge
    // readiness on the already-open serial connection.
    mrspifsBootMagic = 0;
    mrspifsSend("OK|REBOOT_MAINT");
    delay(25);
    enterSerialMaintenanceMode();
    return;
  }
  if (line == "MAINT" || line == "MAINT_BEGIN" || line == "ENTER_MAINT") {
    mrspifsSend("OK|MAINT");
    delay(25);
    enterSerialMaintenanceMode();
    return;
  }
  if (line == "HELLO") { mrspifsSend("OK|HELLO"); return; }
  if (line == "BEGIN" || line == "MRSPIFS:BEGIN") { mrspifsSend("OK|BEGIN"); return; }
  if (line == "PING") { mrspifsSend("OK|PING"); return; }
  if (line == "LIST") { mrspifsListDir("/"); return; }
  if (line == "WRITE_ABORT") { mrspifsAbortWrite(); mrspifsSend("OK|WRITE_ABORT"); return; }
  if (line == "FORMAT" || line == "FS_FORMAT") {
    audio.stopSong();
    bool ok = SPIFFS.format();
    mrspifsSend(ok ? "OK|FORMAT" : "ERR|FORMAT|failed");
    return;
  }
  if (line == "REBOOT") { mrspifsSend("OK|REBOOT"); delay(150); ESP.restart(); return; }

  if (line.startsWith("READ|PATH|")) {
    String path = normalizeFsPath(line.substring(10));
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) { mrspifsSend("ERR|READ|open_failed"); return; }

    mrspifsSend("READ_BEGIN|" + path + "|" + String((uint32_t)f.size()));
    uint8_t inbuf[96];
    unsigned char outbuf[160];
    while (f.available()) {
      size_t n = f.read(inbuf, sizeof(inbuf));
      size_t olen = 0;
      int rc = mbedtls_base64_encode(outbuf, sizeof(outbuf), &olen, inbuf, n);
      if (rc != 0) { f.close(); mrspifsSend("ERR|READ|b64_failed"); return; }
      String b64;
      b64.reserve(olen + 1);
      for (size_t i = 0; i < olen; i++) b64 += (char)outbuf[i];
      mrspifsSend("DATA|" + b64);
      yield();
    }
    f.close();
    mrspifsSend("OK|READ_END");
    return;
  }

  if (line.startsWith("WRITE_BEGIN|PATH|")) {
    mrspifsAbortWrite();
    String args = line.substring(17);
    int sizeTag = args.lastIndexOf("|SIZE|");
    if (sizeTag >= 0) {
      mrspifsWritePath = normalizeFsPath(args.substring(0, sizeTag));
      mrspifsExpectedSize = (size_t)args.substring(sizeTag + 6).toInt();
    } else {
      int sizeSep = args.lastIndexOf('|');
      if (sizeSep <= 0) { mrspifsSend("ERR|WRITE_BEGIN|bad_args"); return; }
      mrspifsWritePath = normalizeFsPath(args.substring(0, sizeSep));
      mrspifsExpectedSize = (size_t)args.substring(sizeSep + 1).toInt();
    }
    if (mrspifsExpectedSize == 0) {
      mrspifsSend("ERR|WRITE_BEGIN|missing_size");
      return;
    }

    audio.stopSong();
    delay(50);
    if (SPIFFS.exists(mrspifsWritePath)) SPIFFS.remove(mrspifsWritePath);
    mrspifsWriteFile = SPIFFS.open(mrspifsWritePath, FILE_WRITE);
    if (!mrspifsWriteFile) { mrspifsSend("ERR|WRITE_BEGIN|open_failed"); return; }

    mrspifsWrittenSize = 0;
    mrspifsWriting = true;
    mrspifsLastActivityMs = millis();
    uiDirty = false;
    mrspifsSend("OK|WRITE_BEGIN");
    return;
  }

  if (line.startsWith("WRITE_DATA|B64|")) {
    if (!mrspifsWriting || !mrspifsWriteFile) { mrspifsSend("ERR|WRITE_DATA|not_open"); return; }
    String b64 = line.substring(15);
    size_t outLen = (b64.length() * 3) / 4 + 4;
    uint8_t* out = (uint8_t*)malloc(outLen);
    if (!out) { mrspifsSend("ERR|WRITE_DATA|no_mem"); return; }

    size_t decodedLen = 0;
    int rc = mbedtls_base64_decode(out, outLen, &decodedLen, (const unsigned char*)b64.c_str(), b64.length());
    if (rc != 0) { free(out); mrspifsSend("ERR|WRITE_DATA|b64_failed"); return; }
    size_t w = mrspifsWriteFile.write(out, decodedLen);
    free(out);
    yield();
    if (w != decodedLen) {
      mrspifsSend("ERR|WRITE_DATA|write_failed|" + mrspifsWritePath + "|w=" + String((uint32_t)w) + "|expected=" + String((uint32_t)decodedLen) + "|fs=" + spiffsUsageText());
      return;
    }

    // Do NOT flush after every base64 data packet.  SPIFFS flush is very slow
    // and the PC tool waits for OK after each packet, so per-packet flushing
    // can reduce upload speed to a few tenths of a KB/s.  Flush once at
    // WRITE_END instead.
    mrspifsWrittenSize += w;
    mrspifsLastActivityMs = millis();
    mrspifsSend("OK|WRITE_DATA");
    return;
  }

  if (line == "WRITE_END") {
    if (!mrspifsWriting || !mrspifsWriteFile) { mrspifsSend("ERR|WRITE_END|not_open"); return; }
    mrspifsWriteFile.flush();
    mrspifsWriteFile.close();

    bool sizeOk = (mrspifsExpectedSize == mrspifsWrittenSize);
    String finishedPath = mrspifsWritePath;
    size_t finishedExpectedSize = mrspifsExpectedSize;
    size_t finishedWrittenSize = mrspifsWrittenSize;
    mrspifsAbortWrite(false);
    mrspifsLastActivityMs = millis();

    if (!sizeOk) {
      SPIFFS.remove(finishedPath);
      mrspifsSend("ERR|WRITE_END|size_mismatch|" + finishedPath + "|written=" + String((uint32_t)finishedWrittenSize) + "|expected=" + String((uint32_t)finishedExpectedSize) + "|fs=" + spiffsUsageText());
      return;
    }

    if (finishedPath == STATIONS_FILE && (selectedCountry == "local" || selectedGenre == "local" || usingLocalStations)) {
      if (loadStationsFromSPIFFS()) {
        previewStation = 0;
        currentStation = 0;
        playStation(0);
      }
    }
    mrspifsSend("OK|WRITE_END");
    return;
  }

  if (line.startsWith("DELETE|PATH|")) {
    String path = normalizeFsPath(line.substring(12));
    int removed = mrspifsRemovePathWithAliases(path);
    if (removed > 0) mrspifsSend("OK|DELETE|" + String(removed));
    else mrspifsSend("OK|DELETE|0");
    return;
  }

  // SPIFFS is flat, but the PC tool may send these when uploading folders.
  // Accept them for compatibility so the queue does not stop.
  if (line.startsWith("MKDIR|PATH|")) { mrspifsSend("OK|MKDIR"); return; }
  if (line.startsWith("RMDIR|PATH|")) {
    String path = normalizeFsPath(line.substring(11));
    int removed = mrspifsRemovePathWithAliases(path);
    if (removed > 0) mrspifsSend("OK|RMDIR|" + String(removed));
    else mrspifsSend("OK|RMDIR|0");
    return;
  }

  mrspifsSend("ERR|UNKNOWN|" + line);
}

void handleSerialMaintenance() {
  static String line;
  static bool lineReserved = false;
  static uint16_t rxYieldCounter = 0;
  if (!lineReserved) {
    // Avoid repeated heap reallocations while receiving WRITE_DATA lines.
    // Keep this modest; it will still grow if the PC tool sends larger chunks.
    line.reserve(8192);
    lineReserved = true;
  }
  while (Serial.available()) {
    if ((++rxYieldCounter & 0x00FF) == 0) yield();
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (mrspifsLineOverflow) {
        mrspifsLineOverflow = false;
        line = "";
        if (mrspifsWriting) {
          String partialPath = mrspifsWritePath;
          mrspifsAbortWrite();
          mrspifsSend("ERR|LINE|too_long|" + partialPath);
        } else {
          mrspifsSend("ERR|LINE|too_long");
        }
        continue;
      }
      handleSerialMaintenanceLine(line);
      line = "";
    } else {
      if (line.length() < MRSPIFS_MAX_LINE) {
        line += c;
      } else {
        mrspifsLineOverflow = true;
      }
    }
  }
}

void enterSerialMaintenanceMode() {
  serialMaintenanceOnly = true;
  mrspifsBootMagic = 0;

  // Keep the upload path clean and quiet: no WiFi, no audio, no web server,
  // no encoder task, no normal UI loop while the PC tool is writing SPIFFS.
  audio.stopSong();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  if (!SPIFFS.begin(true)) {
    mrspifsSend("ERR|MAINT|spiffs_mount_failed");
  } else {
    mrspifsSend("OK|MAINT_READY|fs=" + spiffsUsageText());
  }

  uint32_t lastAliveMs = 0;
  for (;;) {
    handleSerialMaintenance();
    if (millis() - lastAliveMs > 5000UL) {
      lastAliveMs = millis();
      // Quiet heartbeat, useful for seeing that the firmware did not reboot.
      // Partition Manager should ignore MRSPIFS status lines it did not ask for.
      // If your PC tool dislikes this, comment out the next line.
      // mrspifsSend("OK|MAINT_ALIVE");
    }
    delay(1);
    yield();
  }
}

void serviceMaintenanceFor(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    handleSerialMaintenance();
    delay(10);
    yield();
  }
}


String extractQuotedMetaField(const String& src, const char* key) {
  String lower = src;
  lower.toLowerCase();
  String k = String(key);
  k.toLowerCase();

  int pos = lower.indexOf(k + "=\"");
  int quoteOffset = 1;
  if (pos < 0) {
    pos = lower.indexOf(k + "='");
    quoteOffset = 1;
  }
  if (pos < 0) return "";

  int start = src.indexOf((quoteOffset == 1 && lower.indexOf(k + "='", pos) == pos) ? '\'' : '"', pos);
  if (start < 0) return "";
  char q = src[start];
  int end = src.indexOf(q, start + 1);
  if (end <= start) return "";

  String out = src.substring(start + 1, end);
  out.trim();
  return asciiText(out);
}

bool setMetadataFromKeyValueLine(const String& rawInfo) {
  // Newer ESP32-audioI2S can expose HLS metadata as:
  // title="Song",artist="Artist",url="..."
  String artist = extractQuotedMetaField(rawInfo, "artist");
  String title = extractQuotedMetaField(rawInfo, "title");

  if (artist.length() == 0 && title.length() == 0) return false;

  if (artist == "Unknown") artist = "";
  if (title == "Unknown") title = "";
  metaArtist = artist;
  metaTitle = title;
  metaAvailable = (metaArtist.length() > 0 || metaTitle.length() > 0);

  Serial.print("[META kv parsed] artist='");
  Serial.print(metaArtist);
  Serial.print("' title='");
  Serial.print(metaTitle);
  Serial.println("'");
  uiDirty = true;
  return true;
}

String stripIcyStreamTitle(String info) {
  info.trim();

  // Some streams send the full ICY block, e.g. StreamTitle='Artist - Title';
  int key = info.indexOf("StreamTitle=");
  if (key >= 0) {
    int start = info.indexOf('\'', key);
    int end = info.indexOf("';", start + 1);
    if (start >= 0 && end > start) {
      info = info.substring(start + 1, end);
    }
  }

  info.replace("\\'", "'");
  info.replace("\"", "");
  info.trim();
  return asciiText(info);
}

void clearMetadata() {
  metaArtist = "";
  metaTitle = "";
  metaAvailable = false;
  lastMetaRaw = "";
}

bool looksLikeRealMetadata(const String& info) {
  if (info.length() < 3) return false;
  String l = info;
  l.toLowerCase();
  if (l.indexOf("streamtitle=") >= 0) return true;
  if (l.indexOf("title:") >= 0) return true;
  if (l.indexOf("artist:") >= 0) return true;
  if (info.indexOf(" - ") > 0) return true;
  if (info.indexOf(" – ") > 0) return true;
  if (info.indexOf(" — ") > 0) return true;
  return false;
}

String stripMetaPrefix(String info, const char* prefix) {
  String p = prefix;
  String lower = info;
  lower.toLowerCase();
  p.toLowerCase();
  int pos = lower.indexOf(p);
  if (pos >= 0) {
    info = info.substring(pos + String(prefix).length());
    info.trim();
  }
  return info;
}

void setMetadataFromStreamTitle(const String& rawInfo) {
  lastMetaRaw = rawInfo;
  if (setMetadataFromKeyValueLine(rawInfo)) return;
  String info = stripIcyStreamTitle(rawInfo);
  info = stripMetaPrefix(info, "StreamTitle=");
  info = stripMetaPrefix(info, "Title:");
  info.trim();

  if (info.length() == 0 || info == "Unknown") {
    clearMetadata();
    return;
  }

  // Common ICY format: Artist - Title.
  int sep = info.indexOf(" - ");
  if (sep < 0) sep = info.indexOf(" – ");
  if (sep < 0) sep = info.indexOf(" — ");

  if (sep > 0) {
    metaArtist = info.substring(0, sep);
    metaTitle = info.substring(sep + 3);
    metaArtist.trim();
    metaTitle.trim();
  } else {
    // If the stream does not split artist/title, show the whole metadata as title.
    metaArtist = "";
    metaTitle = info;
  }

  metaArtist = asciiText(metaArtist);
  metaTitle = asciiText(metaTitle);
  if (metaArtist == "Unknown") metaArtist = "";
  if (metaTitle == "Unknown") metaTitle = "";
  metaAvailable = (metaArtist.length() > 0 || metaTitle.length() > 0);

  Serial.print("[META parsed] artist='");
  Serial.print(metaArtist);
  Serial.print("' title='");
  Serial.print(metaTitle);
  Serial.println("'");
}

void setMetadataFromId3Line(const String& rawInfo) {
  String info = rawInfo;
  info.trim();
  String lower = info;
  lower.toLowerCase();

  Serial.print("[META id3] ");
  Serial.println(info);

  if (lower.startsWith("artist:")) {
    metaArtist = asciiText(info.substring(info.indexOf(':') + 1));
    if (metaArtist == "Unknown") metaArtist = "";
    metaAvailable = true;
    uiDirty = true;
    return;
  }

  if (lower.startsWith("title:")) {
    metaTitle = asciiText(info.substring(info.indexOf(':') + 1));
    if (metaTitle == "Unknown") metaTitle = "";
    metaAvailable = true;
    uiDirty = true;
    return;
  }

  if (looksLikeRealMetadata(info)) {
    setMetadataFromStreamTitle(info);
    uiDirty = true;
  }
}

void saveLastStation(int i) {
  if (i < 0 || i >= stationCount) return;
  prefs.begin("radio", false);
  prefs.putString("url", stations[i].url);
  prefs.putString("name", stations[i].name);
  prefs.putString("country", selectedCountry);
  prefs.putString("genre", selectedGenre);
  prefs.putInt("offset", rbOffset);
  prefs.end();

  lastStationUrl = stations[i].url;
  lastStationName = stations[i].name;
  lastStationCountry = selectedCountry;
  lastStationOffset = rbOffset;

  Serial.print("Saved last station: ");
  Serial.print(lastStationName);
  Serial.print(" | offset: ");
  Serial.println(lastStationOffset);
}

void loadStartupPrefs() {
  prefs.begin("radio", true);
  lastStationUrl = prefs.getString("url", "");
  lastStationName = prefs.getString("name", "");
  lastStationCountry = prefs.getString("country", "HU");
  lastStationOffset = prefs.getInt("offset", 0);
  selectedGenre = prefs.getString("genre", "all");
  prefs.end();

  // Requested startup behavior: always start from Hungarian stations.
  selectedCountry = "HU";
  previewCountry = selectedCountry;
  previewTag = selectedGenre;
  searchTag = selectedGenre;

  if (lastStationOffset < 0) lastStationOffset = 0;
  lastStationOffset = (lastStationOffset / RB_PAGE_SIZE) * RB_PAGE_SIZE;
  restoreLastStationPending = (lastStationUrl.length() > 0 && lastStationCountry == "HU");
  if (!restoreLastStationPending) lastStationOffset = 0;

  Serial.print("Startup country: ");
  Serial.println(selectedCountry);
  if (restoreLastStationPending) {
    Serial.print("Last station to restore: ");
    Serial.print(lastStationName);
    Serial.print(" | offset: ");
    Serial.println(lastStationOffset);
  }
}

int findStationByUrl(const String& url) {
  for (int i = 0; i < stationCount; i++) {
    if (stations[i].url == url) return i;
  }
  return -1;
}

bool fetchStations(String tag, String country) {
  // Backward compatibility: selecting Local either as country or as genre uses SPIFFS.
  if (country == "local" || tag == "local") {
    return loadStationsFromSPIFFS();
  }

  usingLocalStations = false;
  // Stop the current stream before opening a second network connection.
  // This avoids ESP32-S3 N4R2 reboots / HTTP -1 errors during country changes.
  audio.stopSong();
  delay(150);
  yield();

  // Always use /search. The bycountrycodeexact endpoint can return a much larger
  // response on some mirrors and can overflow/fragment RAM on small S3 boards.
  String url = String(RB_HOST) + "/json/stations/search?hidebroken=true&order=clickcount&reverse=true&limit=" + String(RB_API_LIMIT) + "&offset=" + String(rbOffset);
  if (country != "all") {
    country.toUpperCase();
    url += "&countrycode=" + country;
  }
  if (tag != "all") url += "&tag=" + tag;
  if (bitrateCap > 0) url += "&bitrateMax=" + String(bitrateCap);

  Serial.println();
  Serial.println("Radio Browser query:");
  Serial.println(url);

  int code = -1;
  DynamicJsonDocument doc(24576);

  for (int attempt = 1; attempt <= 2; attempt++) {
    HTTPClient http;
    http.setTimeout(8000);
    http.setConnectTimeout(5000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);
    http.begin(url);
    http.addHeader("User-Agent", "ESP32S3-SuperMini-InternetRadio/1.0");
    http.addHeader("Connection", "close");

    code = http.GET();
    Serial.print("HTTP attempt ");
    Serial.print(attempt);
    Serial.print(" code: ");
    Serial.println(code);

    if (code == HTTP_CODE_OK) {
      DeserializationError err = deserializeJson(doc, http.getStream());
      http.end();
      if (err) {
        Serial.print("JSON error: ");
        Serial.println(err.c_str());
        streamTitle = "JSON ";
        streamTitle += err.c_str();
        uiDirty = true;
        return false;
      }
      break;
    }

    http.end();
    delay(600);
    yield();
  }

  if (code != HTTP_CODE_OK) {
    streamTitle = "HTTP " + String(code);
    uiDirty = true;
    return false;
  }

  stationCount = 0;
  int seen = 0;
  int skipped = 0;

  for (JsonObject s : doc.as<JsonArray>()) {
    yield();
    seen++;
    String resolved = s["url_resolved"] | "";
    String plain = s["url"] | "";
    String codec = s["codec"] | "";
    int hls = s["hls"] | 0;

    String u = resolved.length() ? resolved : plain;
    String lr = resolved;
    lr.toLowerCase();
    String lp = plain;
    lp.toLowerCase();
    if ((lr.indexOf("flac") >= 0 || lr.indexOf("ogg") >= 0) &&
        (lp.indexOf(".mp3") >= 0 || lp.indexOf("aac") >= 0)) {
      u = plain;
    }
    u = cleanStreamUrl(u);

    if (!(u.startsWith("http://") || u.startsWith("https://"))) { skipped++; continue; }
    if (hls != 0) { skipped++; continue; }
    if (!supportedCodec(codec, u)) { skipped++; continue; }

    stations[stationCount].name = asciiText(String(s["name"] | "Unknown"));
    stations[stationCount].url = u;
    stations[stationCount].bitrate = s["bitrate"] | 0;
    stations[stationCount].codec = normalizeCodecName(codec.length() ? codec : guessCodecFromUrl(u));
    stations[stationCount].country = String(s["countrycode"] | "");

    Serial.print("+");
    Serial.print(stationCount + 1);
    Serial.print(" ");
    Serial.print(stations[stationCount].name);
    Serial.print(" | ");
    Serial.println(stations[stationCount].url);

    stationCount++;
    if (stationCount >= RB_RESULT_LIMIT) break;
  }

  Serial.print("Stations in JSON: ");
  Serial.print(seen);
  Serial.print(" | stored: ");
  Serial.print(stationCount);
  Serial.print(" | skipped: ");
  Serial.println(skipped);
  Serial.print("Page: ");
  Serial.print(rbPage + 1);
  Serial.print(" | offset: ");
  Serial.println(rbOffset);

  // Last-page detection must be based on the number of entries returned by the API,
  // not on stationCount. stationCount can be lower because unsupported
  // codecs/URLs are filtered out, but there may still be a next API page.
  rbLastPageShort = (seen < RB_API_LIMIT);

  if (stationCount == 0) {
    streamTitle = "No playable stations";
    uiDirty = true;
    return false;
  }
  return true;
}


bool loadStationsPage(int newOffset) {
  if (newOffset < 0) newOffset = 0;

  int oldOffset = rbOffset;
  int oldPage = rbPage;
  rbOffset = newOffset;
  rbPage = rbOffset / RB_PAGE_SIZE;

  streamTitle = String("Loading p") + String(rbPage + 1) + "...";
  currentStation = 0;
  previewStation = 0;
  uiDirty = true;
  drawUI();

  if (fetchStations(selectedGenre, selectedCountry)) {
    int startIndex = 0;
    if (restoreLastStationPending) {
      int restored = findStationByUrl(lastStationUrl);
      if (restored >= 0) {
        startIndex = restored;
        Serial.print("Restored last station on this page: ");
        Serial.println(stations[startIndex].name);
      } else {
        Serial.println("Last station not found on this page; using first playable station.");
      }
      restoreLastStationPending = false;
    }
    previewStation = startIndex;
    playStation(startIndex);
    return true;
  }

  rbOffset = oldOffset;
  rbPage = oldPage;
  return false;
}

void nextStationsPage() {
  if (usingLocalStations) {
    int nextOffset = rbOffset + RB_PAGE_SIZE;
    Serial.print("Next local SPIFFS page request, offset: ");
    Serial.println(nextOffset);

    if (rbLastPageShort) {
      nextOffset = 0;
      Serial.println("Last local page reached, wrapping to first page.");
    }

    audio.stopSong();
    delay(60);
    if (!loadStationsFromSPIFFS(nextOffset)) {
      Serial.println("Next local page failed or empty, wrapping to first page.");
      loadStationsFromSPIFFS(0);
    }

    previewStation = 0;
    currentStation = 0;
    if (stationCount > 0) playStation(0);
    return;
  }

  int nextOffset = rbOffset + RB_PAGE_SIZE;
  Serial.print("Next page request, offset: ");
  Serial.println(nextOffset);

  if (rbLastPageShort) {
    nextOffset = 0;
    Serial.println("Last page reached, wrapping to first page.");
  }

  if (!loadStationsPage(nextOffset)) {
    Serial.println("Next page failed or empty, wrapping to first page.");
    loadStationsPage(0);
  }
}


void playStation(int i) {
  if (i < 0 || i >= stationCount) return;
  currentStation = i;
  clearMetadata();
  currentBitrateKbps = stations[i].bitrate;
  currentCodecName = normalizeCodecName(stations[i].codec.length() ? stations[i].codec : guessCodecFromUrl(stations[i].url));
  lastAudioStatusPollMs = 0;
  streamTitle = "Connecting...";
  audio.connecttohost(stations[i].url.c_str());
  saveLastStation(i);
  uiDirty = true;
}

String countryName(String c) {
  c.toUpperCase();
  if (c == "LOCAL") return "Local";
  if (c == "HU") return "Hungary";
  if (c == "US") return "USA";
  if (c == "GB") return "UK";
  if (c == "IN") return "India";
  if (c == "DE") return "Germany";
  if (c == "FR") return "France";
  if (c == "JP") return "Japan";
  if (c == "CA") return "Canada";
  if (c == "AU") return "Australia";
  if (c == "IT") return "Italy";
  if (c == "ES") return "Spain";
  if (c == "BR") return "Brazil";
  if (c == "MX") return "Mexico";
  if (c == "NL") return "Netherlands";
  if (c == "SE") return "Sweden";
  if (c == "NO") return "Norway";
  if (c == "ZA") return "South Africa";
  if (c == "SG") return "Singapore";
  if (c == "AE") return "UAE";
  return c;
}

String typeName() {
  for (int i = 0; i < GENRE_COUNT; i++)
    if (selectedGenre == GENRES[i].tag)
      return String(GENRES[i].label);
  return String(selectedGenre);  // fallback: show raw tag
}

#define DISP canvas

int ui160ChipTextWidth(const String& txt, Ui160FontRole role) {
  return ui160TextWidth(DISP, txt, role);
}

void drawUi160Text(Ui160FontRole role, int x, int y, const String& txt, uint16_t color) {
  bool vlwOk = ui160UseFont(DISP, role);
  DISP.setTextColor(color);
  DISP.setTextDatum(TL_DATUM);
  if (vlwOk) {
    DISP.drawString(txt, x, y);
    DISP.unloadFont();
  } else {
    DISP.setCursor(x, y + 2);
    DISP.print(txt);
  }
}

int chipWidth(const String& txt) {
  return ui160ChipTextWidth(txt, UI160_FONT_BOLD) + 10;
}

void chip(int x, int y, String txt, uint16_t col) {
  int w = chipWidth(txt);
  bool activeEdit = (uiMode == MODE_EDIT && ((focusIndex == 1 && x < 60) || (focusIndex == 2 && x > 60)));
  uint16_t textCol = activeEdit ? ST77XX_BLACK : col;
  if (activeEdit) {
    DISP.fillRoundRect(x, y, w, 14, 3, col);
  } else {
    DISP.drawRoundRect(x, y, w, 14, 3, col);
  }
  drawUi160Text(UI160_FONT_BOLD, x + 5, y, txt, textCol);
}

int statusChipWidth(const String& txt) {
  // Slightly tighter than the outer chips so labels like "1.4M FLAC" fit
  // between the left country and right genre boxes on the 160 px display.
  return ui160ChipTextWidth(txt, UI160_FONT_BOLD) + 6;
}

void statusChip(int x, int y, String txt, uint16_t col) {
  int w = statusChipWidth(txt);
  DISP.drawRoundRect(x, y, w, 14, 3, col);
  drawUi160Text(UI160_FONT_BOLD, x + 3, y, txt, col);
}


void setBacklight(uint8_t value) {
  // T-Display S3: GPIO38 = háttérvilágítás (PWM), GPIO15 = tápkapcsoló (HIGH = be)
  // GPIO15-öt egyszer HIGH-ra kell állítani az initben; itt csak a fényerőt kezeljük.
  analogWrite(38, value);
}

void updateBacklight() {
  bool shouldDim = (millis() - lastUserActionMs >= BACKLIGHT_DIM_AFTER);
  if (shouldDim != backlightDimmed) {
    backlightDimmed = shouldDim;
    setBacklight(backlightDimmed ? BACKLIGHT_DIM : BACKLIGHT_FULL);
  }
}

void resetStationScroll(const String& text, int stationIndex) {
  scrollLastStation = stationIndex;
  scrollLastText = text;
  scrollStartMs = millis();
}

void printScrolledText(int x, int y, int w, const String& text, uint16_t color) {
  const int lineH = 16;
  Ui160FontRole role = UI160_FONT_BOLD;
  int textW = ui160TextWidth(stationSelectCanvas, text, role);
  int tx = 0;

  if (textW <= w) {
    tx = (w - textW) / 2;
  } else {
    if (text != scrollLastText) resetStationScroll(text, scrollLastStation);

    int maxOffset = textW - w + 6;
    uint32_t runMs = (uint32_t)maxOffset * STATION_SCROLL_STEP_MS;
    uint32_t cycleMs = runMs + STATION_SCROLL_PAUSE_MS;
    uint32_t t = (millis() - scrollStartMs) % cycleMs;
    tx = -((t < runMs) ? (int)(t / STATION_SCROLL_STEP_MS) : 0);
  }

  stationSelectCanvas.setTextWrap(false);
  stationSelectCanvas.fillScreen(ST77XX_YELLOW);
  bool vlwOk = ui160UseFont(stationSelectCanvas, role);
  stationSelectCanvas.setTextColor(color);
  stationSelectCanvas.setTextDatum(TL_DATUM);
  if (vlwOk) {
    stationSelectCanvas.drawString(text, tx, 1);
    stationSelectCanvas.unloadFont();
  } else {
    stationSelectCanvas.setCursor(tx, 3);
    stationSelectCanvas.print(text);
  }
  DISP.pushImage(x, y, w, lineH, (uint16_t*)stationSelectCanvas.getBuffer());
}


Ui160FontRole infoFontRole(int line) {
  if (line == INFO_LINE_STATION) return UI160_FONT_STATION;
  if (line == INFO_LINE_ARTIST) return UI160_FONT_ARTIST;
  if (line == INFO_LINE_TITLE) return UI160_FONT_TITLE;
  return UI160_FONT_INFO;
}

int textPixelWidthRole(const String& text, Ui160FontRole role) {
  return ui160TextWidth(DISP, text, role);
}

int textPixelWidth(const String& text) {
  return textPixelWidthRole(text, UI160_FONT_INFO);
}

void drawClippedInfoText(int line, int x, int y, int w, const String& text, int offset, uint16_t color, bool centered) {
  const int lineW = 144;
  const int lineH = 16;
  uint16_t cardBg = tft.color565(32, 73, 93);

  // The metadata box is fixed at 144 px in this layout. If a future tweak calls
  // this with another width, keep the drawing safe instead of corrupting rows.
  if (w != lineW) w = lineW;

  Ui160FontRole role = infoFontRole(line);
  int textW = textPixelWidthRole(text, role);
  if (line == INFO_LINE_STATION && (textW > w - 4 || text.length() > 24)) centered = false;
  int tx = centered ? (w - textW) / 2 : -offset;
  if (tx > w) tx = w;

  if (line == INFO_LINE_STATION) {
    const int stationLineH = 18;
    infoLineCanvas.setTextWrap(false);
    infoLineCanvas.fillScreen(cardBg);
    bool vlwOk = ui160UseFont(infoLineCanvas, UI160_FONT_STATION);
    infoLineCanvas.setTextColor(color);
    infoLineCanvas.setTextDatum(TL_DATUM);
    if (vlwOk) {
      infoLineCanvas.drawString(text, tx, 0);
    } else {
      infoLineCanvas.setCursor(tx, 2);
      infoLineCanvas.print(text);
    }
    DISP.pushImage(x, y - 1, w, stationLineH, (uint16_t*)infoLineCanvas.getBuffer());
    infoLineCanvas.unloadFont();
    return;
  }

  infoLineCanvas.setTextWrap(false);
  infoLineCanvas.fillScreen(cardBg);
  bool vlwOk = ui160UseFont(infoLineCanvas, role);
  infoLineCanvas.setTextColor(color);
  infoLineCanvas.setTextDatum(TL_DATUM);

  if (vlwOk) {
    infoLineCanvas.drawString(text, tx, 0);
  } else {
    infoLineCanvas.setCursor(tx, 1);
    infoLineCanvas.print(text);
  }

  DISP.pushImage(x, y, w, lineH, (uint16_t*)infoLineCanvas.getBuffer());
  infoLineCanvas.unloadFont();
}

bool infoLineLong(int line, const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  if (line == INFO_LINE_STATION) return textPixelWidthRole(stationLine, UI160_FONT_STATION) > w;
  if (line == INFO_LINE_ARTIST)  return textPixelWidthRole(artistLine, UI160_FONT_ARTIST) > w;
  if (line == INFO_LINE_TITLE)   return textPixelWidthRole(titleLine, UI160_FONT_TITLE) > w;
  return false;
}

String infoLineText(int line, const String& stationLine, const String& artistLine, const String& titleLine) {
  if (line == INFO_LINE_STATION) return stationLine;
  if (line == INFO_LINE_ARTIST)  return artistLine;
  if (line == INFO_LINE_TITLE)   return titleLine;
  return "";
}

int firstLongInfoLine(const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  if (infoLineLong(INFO_LINE_STATION, stationLine, artistLine, titleLine, w)) return INFO_LINE_STATION;
  if (infoLineLong(INFO_LINE_ARTIST,  stationLine, artistLine, titleLine, w)) return INFO_LINE_ARTIST;
  if (infoLineLong(INFO_LINE_TITLE,   stationLine, artistLine, titleLine, w)) return INFO_LINE_TITLE;
  return -1;
}

int nextLongInfoLine(int current, const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  for (int n = 1; n <= 3; n++) {
    int candidate = (current + n) % 3;
    if (infoLineLong(candidate, stationLine, artistLine, titleLine, w)) return candidate;
  }
  return -1;
}

void updateInfoScrollState(const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  String key = stationLine + "\n" + artistLine + "\n" + titleLine;
  if (key != infoScrollKey) {
    infoScrollKey = key;
    infoScrollLine = firstLongInfoLine(stationLine, artistLine, titleLine, w);
    infoScrollStartMs = millis();
    return;
  }

  if (infoScrollLine < 0 || !infoLineLong(infoScrollLine, stationLine, artistLine, titleLine, w)) {
    infoScrollLine = firstLongInfoLine(stationLine, artistLine, titleLine, w);
    infoScrollStartMs = millis();
    return;
  }

  String activeText = infoLineText(infoScrollLine, stationLine, artistLine, titleLine);
  int maxOffset = textPixelWidthRole(activeText, infoFontRole(infoScrollLine)) - w + 6;
  uint32_t runMs = (uint32_t)maxOffset * INFO_SCROLL_STEP_MS;
  uint32_t cycleMs = runMs + INFO_SCROLL_PAUSE_MS;

  if (millis() - infoScrollStartMs >= cycleMs) {
    infoScrollLine = nextLongInfoLine(infoScrollLine, stationLine, artistLine, titleLine, w);
    infoScrollStartMs = millis();
  }
}

void printInfoTextLine(int line, int x, int y, int w, const String& text, uint16_t color) {
  // Always redraw the whole text slot through a small off-screen canvas.
  // Without this clipping, Adafruit_GFX can wrap/paint stray glyph fragments
  // at the left side while the long artist/title line is scrolling.
  if (text.length() == 0) {
    drawClippedInfoText(line, x, y, w, "", 0, color, true);
    return;
  }

  Ui160FontRole role = infoFontRole(line);
  int textW = textPixelWidthRole(text, role);
  if (textW <= w) {
    bool centerText = true;
    if (line == INFO_LINE_STATION && (textW > w - 4 || text.length() > 24)) centerText = false;
    drawClippedInfoText(line, x, y, w, text, 0, color, centerText);
    return;
  }

  int offset = 0;
  if (line == infoScrollLine) {
    int maxOffset = textW - w + 6;
    uint32_t runMs = (uint32_t)maxOffset * INFO_SCROLL_STEP_MS;
    uint32_t t = millis() - infoScrollStartMs;
    offset = (t < runMs) ? (int)(t / INFO_SCROLL_STEP_MS) : 0;
  }

  drawClippedInfoText(line, x, y, w, text, offset, color, false);
}

void drawPauseOverlay() {
  if (!muted || uiMode != MODE_NORMAL) return;
  uint16_t panel = tft.color565(35, 35, 45);
  uint16_t edge = ST77XX_YELLOW;
  int x = 52, y = 62, w = 56, h = 22;
  DISP.fillRoundRect(x, y, w, h, 4, panel);
  DISP.drawRoundRect(x, y, w, h, 4, edge);
  int pauseW = ui160TextWidth(DISP, "PAUSE", UI160_FONT_BOLD);
  drawUi160Text(UI160_FONT_BOLD, x + (w - pauseW) / 2, y + 4, "PAUSE", edge);
}

#if SMART_WEBRADIO_DISPLAY_320X240
Ui320FontRole ui320InfoFontRole(int line) {
  if (line == INFO_LINE_STATION) return UI320_FONT_STATION;
  if (line == INFO_LINE_ARTIST) return UI320_FONT_ARTIST;
  if (line == INFO_LINE_TITLE) return UI320_FONT_TITLE;
  return UI320_FONT_INFO;
}

int ui320InfoTextWidth(const String& text, int line) {
  return ui320TextWidth(DISP, text, ui320InfoFontRole(line));
}

bool ui320InfoLineLong(int line, const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  if (line == INFO_LINE_STATION) return ui320InfoTextWidth(stationLine, line) > w;
  if (line == INFO_LINE_ARTIST)  return ui320InfoTextWidth(artistLine, line) > w;
  if (line == INFO_LINE_TITLE)   return ui320InfoTextWidth(titleLine, line) > w;
  return false;
}

int firstLongUi320InfoLine(const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  if (ui320InfoLineLong(INFO_LINE_STATION, stationLine, artistLine, titleLine, w)) return INFO_LINE_STATION;
  if (ui320InfoLineLong(INFO_LINE_ARTIST,  stationLine, artistLine, titleLine, w)) return INFO_LINE_ARTIST;
  if (ui320InfoLineLong(INFO_LINE_TITLE,   stationLine, artistLine, titleLine, w)) return INFO_LINE_TITLE;
  return -1;
}

int nextLongUi320InfoLine(int current, const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  for (int n = 1; n <= 3; n++) {
    int candidate = (current + n) % 3;
    if (ui320InfoLineLong(candidate, stationLine, artistLine, titleLine, w)) return candidate;
  }
  return -1;
}

void updateUi320InfoScrollState(const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  String key = stationLine + "\n" + artistLine + "\n" + titleLine;
  if (key != infoScrollKey) {
    infoScrollKey = key;
    infoScrollLine = firstLongUi320InfoLine(stationLine, artistLine, titleLine, w);
    infoScrollStartMs = millis();
    return;
  }

  if (infoScrollLine < 0 || !ui320InfoLineLong(infoScrollLine, stationLine, artistLine, titleLine, w)) {
    infoScrollLine = firstLongUi320InfoLine(stationLine, artistLine, titleLine, w);
    infoScrollStartMs = millis();
    return;
  }

  String activeText = infoLineText(infoScrollLine, stationLine, artistLine, titleLine);
  int maxOffset = ui320InfoTextWidth(activeText, infoScrollLine) - w + 10;
  uint32_t runMs = (uint32_t)maxOffset * INFO_SCROLL_STEP_MS;
  uint32_t cycleMs = runMs + INFO_SCROLL_PAUSE_MS;

  if (millis() - infoScrollStartMs >= cycleMs) {
    infoScrollLine = nextLongUi320InfoLine(infoScrollLine, stationLine, artistLine, titleLine, w);
    infoScrollStartMs = millis();
  }
}

int ui320InfoScrollOffset(const String& text, int line, int w) {
  if (line != infoScrollLine) return 0;

  int textW = ui320InfoTextWidth(text, line);
  if (textW <= w) return 0;

  int maxOffset = textW - w + 10;
  uint32_t runMs = (uint32_t)maxOffset * INFO_SCROLL_STEP_MS;
  uint32_t t = millis() - infoScrollStartMs;
  return (t < runMs) ? (int)(t / INFO_SCROLL_STEP_MS) : 0;
}

int ui320BrowseTextWidth(const String& text) {
  return ui320TextWidth(canvas, text, UI320_FONT_INFO_BOLD);
}

int ui320BrowseScrollOffset(const String& text, int stationIndex, int w) {
  int textW = ui320BrowseTextWidth(text);
  if (textW <= w) return 0;

  if (scrollLastStation != stationIndex || scrollLastText != text) {
    resetStationScroll(text, stationIndex);
  }

  int maxOffset = textW - w + 10;
  uint32_t runMs = (uint32_t)maxOffset * STATION_SCROLL_STEP_MS;
  uint32_t t = (millis() - scrollStartMs) % (runMs + STATION_SCROLL_PAUSE_MS);
  return (t < runMs) ? (int)(t / STATION_SCROLL_STEP_MS) : 0;
}

void drawUI320() {
  Ui320ScreenState state;

  if (wifiSetupModeActive || WiFi.status() != WL_CONNECTED) {
    state.station = "WiFi setup";
    state.artist = "Connect to AP:";
    state.title = "SmartWebRadio-Setup";
    state.country = "192.168.4.1";
    state.genre = "Open setup page";
    state.audioStatus = "SETUP";
    state.hint = "http://192.168.4.1";
    state.browseName = "";
    state.browseCount = "";
    state.volume = audio.getVolume();
    state.volumeMax = VOL_MAX;
    state.mode = (int)MODE_NORMAL;
    state.focusIndex = -1;
    state.muted = muted;
    state.buttonHolding = false;
    state.holdElapsedMs = 0;
    state.scrollLine = -1;
    state.scrollOffset = 0;
    ui320DrawMain(canvas, tft, state);
    canvas.pushSprite(0, 0);
    uiDirty = false;
    return;
  }

  state.station = (stationCount > 0 && currentStation >= 0 && currentStation < stationCount)
                    ? stations[currentStation].name
                    : streamTitle;
  state.station = asciiText(state.station);

  state.artist = asciiText(metaArtist);
  state.title = asciiText(metaTitle);
  if (state.artist.length() == 0 && state.title.length() == 0) {
    if (streamTitle.length() > 0 && streamTitle != "Connecting...") {
      state.title = asciiText(streamTitle);
    } else {
      state.title = "No metadata yet";
    }
  }
  if (state.station == "Unknown") state.station = "";
  if (state.artist == "Unknown") state.artist = "";
  if (state.title == "Unknown") state.title = "";

  String cCode = (uiMode == MODE_EDIT && focusIndex == F_COUNTRY)
                   ? String(previewCountry)
                   : (selectedCountry == "all"
                        ? (stationCount > 0 ? stations[currentStation].country : String("all"))
                        : String(selectedCountry));
  state.country = countryName(cCode);

  if (uiMode == MODE_EDIT && focusIndex == F_TYPE) {
    state.genre = previewTag;
    for (int i = 0; i < GENRE_COUNT; i++) {
      if (previewTag == GENRES[i].tag) {
        state.genre = GENRES[i].label;
        break;
      }
    }
  } else {
    state.genre = typeName();
  }

  state.audioStatus = audioStatusLabel(false);
  if (state.audioStatus.length() == 0) state.audioStatus = audioStatusLabel(true);

  if (uiMode == MODE_NORMAL) state.hint = "Smart WebRadio by gidano";
  else if (uiMode == MODE_BROWSE) state.hint = "Rotate Select";
  else if (focusIndex == F_NOWPLAYING) state.hint = "Long=Next Page";
  else state.hint = "Rotate Change";

  state.browseName = (stationCount > 0 && previewStation >= 0 && previewStation < stationCount)
                       ? asciiText(stations[previewStation].name)
                       : String("No stations");
  state.browseCount = (stationCount > 0)
                        ? String(previewStation + 1) + "/" + String(stationCount) + "  P" + String(rbPage + 1)
                        : String("0/0  P") + String(rbPage + 1);
  state.volume = audio.getVolume();
  state.volumeMax = VOL_MAX;
  state.mode = (int)uiMode;
  state.focusIndex = (int)focusIndex;
  state.muted = muted;
  state.buttonHolding = buttonHolding;
  state.holdElapsedMs = buttonHolding ? (millis() - holdStartMs) : 0;
  state.scrollLine = -1;
  state.scrollOffset = 0;

  if (uiMode == MODE_EDIT && focusIndex == F_NOWPLAYING) {
    const int browseNameW = 192;
    if (ui320BrowseTextWidth(state.browseName) > browseNameW) {
      state.scrollLine = -10;
      state.scrollOffset = ui320BrowseScrollOffset(state.browseName, previewStation, browseNameW);
    } else if (scrollLastStation != previewStation || scrollLastText != state.browseName) {
      resetStationScroll(state.browseName, previewStation);
    }
  } else {
    const int infoW = 280;
    updateUi320InfoScrollState(state.station, state.artist, state.title, infoW);
    state.scrollLine = infoScrollLine;
    if (infoScrollLine == INFO_LINE_STATION) state.scrollOffset = ui320InfoScrollOffset(state.station, INFO_LINE_STATION, infoW);
    else if (infoScrollLine == INFO_LINE_ARTIST) state.scrollOffset = ui320InfoScrollOffset(state.artist, INFO_LINE_ARTIST, infoW);
    else if (infoScrollLine == INFO_LINE_TITLE) state.scrollOffset = ui320InfoScrollOffset(state.title, INFO_LINE_TITLE, infoW);
  }

  ui320DrawMain(canvas, tft, state);
  canvas.pushSprite(0, 0);
  uiDirty = false;
}
#endif

void drawUI() {
#if SMART_WEBRADIO_DISPLAY_320X240
  drawUI320();
  return;
#endif

  DISP.setTextWrap(false);
  DISP.fillScreen(0x0000);

  uint16_t topBg = tft.color565(50, 0, 60);
  uint16_t cardBg = tft.color565(32, 73, 93);
  uint16_t botBg = ST77XX_BLACK;
  uint16_t border = ST77XX_WHITE;
  uint16_t accent = ST77XX_GREEN;
  uint16_t warn = ST77XX_YELLOW;

  // top bar
  DISP.fillRoundRect(2, 2, 156, 16, 3, topBg);
  DISP.drawRoundRect(2, 2, 156, 16, 3, border);

  // center card
  DISP.fillRoundRect(2, 22, 156, 80, 5, cardBg);
  DISP.drawRoundRect(2, 22, 156, 80, 5, border);

  // bottom bar
  DISP.fillRoundRect(2, 106, 156, 20, 4, botBg);
  DISP.drawRoundRect(2, 106, 156, 20, 4,
                     (uiMode == MODE_NORMAL) ? border : accent);

  // logo
  drawUi160Text(UI160_FONT_INFO_BOLD, 8, 2, "Volume", accent);

  // volume bars - pulled closer to the Volume label to make room
  // for the long-press progress indicator at the right end of the top row.
  uint16_t volCol = (uiMode == MODE_NORMAL) ? accent : 0x39E7;
  const int volBarsX = 54;
  const int volBarsY = 7;
  for (int i = 0; i < 12; i++) {
    uint16_t c = (i < audio.getVolume() * 12 / 21) ? volCol : 0x2104;
    DISP.fillRoundRect(volBarsX + i * 5, volBarsY, 4, 6, 1, c);
  }

  // Menu-button long-press progress indicator, moved from the bottom-right
  // hint line to the right end of the Volume row.
  const int holdX = 149;
  const int holdY = 10;
  const int holdR = 6;
  DISP.drawCircle(holdX, holdY, holdR, ST77XX_WHITE);
  if (buttonHolding) {
    int prog = min(holdR, (int)((millis() - holdStartMs) / 116));
    for (int i = 0; i < prog; i++) {
      DISP.fillCircle(holdX, holdY, i, accent);
    }
  }

  // Station / metadata area
  uint16_t npCol = (focusIndex == F_NOWPLAYING && uiMode != MODE_NORMAL)
                     ? warn
                     : ST77XX_WHITE;

  // station name / browse preview
  if (uiMode == MODE_EDIT && focusIndex == F_NOWPLAYING) {
    String np = stations[previewStation].name;
    if (scrollLastStation != previewStation || scrollLastText != np) resetStationScroll(np, previewStation);

    int stationLabelW = ui160TextWidth(DISP, "Station", UI160_FONT_BOLD);
    drawUi160Text(UI160_FONT_BOLD, (160 - stationLabelW) / 2, 28, "Station", npCol);

    DISP.fillRoundRect(12, 44, 136, 16, 3, warn);
    drawUi160Text(UI160_FONT_BOLD, 13, 45, "<", ST77XX_BLACK);
    drawUi160Text(UI160_FONT_BOLD, 142, 45, ">", ST77XX_BLACK);
    printScrolledText(18, 44, 124, np, ST77XX_BLACK);

    // counter  e.g. "3/15"
    String countTxt = String(previewStation + 1) + "/" + String(stationCount) + "  P" + String(rbPage + 1);
    int countW = ui160TextWidth(DISP, countTxt, UI160_FONT_BOLD);
    drawUi160Text(UI160_FONT_BOLD, (160 - countW) / 2, 64, countTxt, warn);
  } else {
    String stationLine = (stationCount > 0 && currentStation >= 0 && currentStation < stationCount)
                           ? stations[currentStation].name
                           : streamTitle;
    stationLine = asciiText(stationLine);

    String artistLine = metaArtist;
    String titleLine = metaTitle;
    if (artistLine.length() == 0 && titleLine.length() == 0) {
      if (streamTitle.length() > 0 && streamTitle != "Connecting...") {
        titleLine = streamTitle;
      } else {
        titleLine = "No metadata yet";
      }
    }

    artistLine = asciiText(artistLine);
    titleLine = asciiText(titleLine);

    if (stationLine == "Unknown") stationLine = "";
    if (artistLine == "Unknown") artistLine = "";
    if (titleLine == "Unknown") titleLine = "";

    const int infoX = 8;
    const int infoW = 144;
    updateInfoScrollState(stationLine, artistLine, titleLine, infoW);

    printInfoTextLine(INFO_LINE_STATION, infoX, 26, infoW, stationLine, npCol);
    printInfoTextLine(INFO_LINE_ARTIST,  infoX, 44, infoW, artistLine, ST77XX_CYAN);
    printInfoTextLine(INFO_LINE_TITLE,   infoX, 60, infoW, titleLine, ST77XX_WHITE);
  }

  // ── Country chip ─────────────────────────────────────────
  // Szerkesztés közben az „Előnézet” országot jelenítsd meg, egyébként a „Kiválasztott országot”
  // (visszatér a lejátszó országához, amikor „all”)
  String cCode = (uiMode == MODE_EDIT && focusIndex == F_COUNTRY)
                   ? String(previewCountry)
                   : (selectedCountry == "all"
                        ? (stationCount > 0 ? stations[currentStation].country : String("all"))
                        : String(selectedCountry));
  String cLabel = countryName(cCode);
  int cChipW = chipWidth(cLabel);
  chip(4, 82, cLabel, focusIndex == F_COUNTRY ? warn : accent);

  // ── Genre chip ───────────────────────────────────────────
  // Show previewTag label while editing, otherwise selectedGenre label
  String gLabel;
  if (uiMode == MODE_EDIT && focusIndex == F_TYPE) {
    // find label for previewTag
    gLabel = previewTag;  // fallback
    for (int i = 0; i < GENRE_COUNT; i++)
      if (previewTag == GENRES[i].tag) {
        gLabel = GENRES[i].label;
        break;
      }
  } else {
    gLabel = typeName();
  }
  int gChipW = chipWidth(gLabel);
  int gChipX = max(4, 156 - gChipW);  // right aligned with 4px margin

  // ── Bitrate / codec status chip, centered in the free space between country and genre ──
  int leftChipRight = 4 + cChipW;
  int rightChipLeft = gChipX;
  int middleCenter = (leftChipRight + rightChipLeft) / 2;

  String aLabel = audioStatusLabel(false);  // e.g. "128k MP3" or "1.4M FLAC"
  int aChipW = statusChipWidth(aLabel);
  int aChipX = middleCenter - aChipW / 2;
  if (leftChipRight + 2 <= aChipX && aChipX + aChipW + 2 <= rightChipLeft) {
    statusChip(aChipX, 82, aLabel, ST77XX_WHITE);
  } else {
    // Very long country / genre labels can leave no middle space. Keep the UI clean.
    aLabel = audioStatusLabel(true);   // shorter: "128k" / "1.4M"
    aChipW = statusChipWidth(aLabel);
    aChipX = middleCenter - aChipW / 2;
    if (leftChipRight + 2 <= aChipX && aChipX + aChipW + 2 <= rightChipLeft) {
      statusChip(aChipX, 82, aLabel, ST77XX_WHITE);
    }
  }

  chip(gChipX, 82, gLabel, focusIndex == F_TYPE ? warn : ST77XX_CYAN);

  // bottom hint
  String hintText;
  if (uiMode == MODE_NORMAL) hintText = "Smart WebRadio by gidano";
  else if (uiMode == MODE_BROWSE) hintText = "Rotate Select";
  else if (focusIndex == F_NOWPLAYING) hintText = "Long=Next Page";
  else hintText = "Rotate Change";
  int hintW = ui160TextWidth(DISP, hintText, UI160_FONT_BOLD);
  int hintX = max(8, (160 - hintW) / 2);
  drawUi160Text(UI160_FONT_BOLD, hintX, 109, hintText, ST77XX_WHITE);

  drawPauseOverlay();

  canvas.pushSprite(0, 0);

  uiDirty = false;
}

void applyChange() {
  previewStation = 0;
  rbOffset = 0;
  rbPage = 0;
  rbLastPageShort = false;
  streamTitle = "Loading...";
  uiDirty = true;
  drawUI();
  Serial.print("Apply country: ");
  Serial.print(selectedCountry);
  Serial.print(" | genre: ");
  Serial.println(selectedGenre);

  // Local is a virtual country: reload stations from SPIFFS without rebooting.
  // Stop the previous online stream first, otherwise a failed/slow local load can
  // leave the old Hungary station audible and make it look like Local was ignored.
  if (selectedCountry == "local" || selectedGenre == "local") {
    audio.stopSong();
    delay(80);
    restoreLastStationPending = false;
    if (loadStationsFromSPIFFS()) {
      previewStation = 0;
      currentStation = 0;
      playStation(0);
    } else {
      stationCount = 0;
      usingLocalStations = false;
      currentStation = 0;
      previewStation = 0;
      selectedCountry = "local";
      previewCountry = "local";
      streamTitle = "No local file";
      uiDirty = true;
    }
    return;
  }

  // Leaving Local through the country list must switch back to the online source.
  // If Genre was still "local", fetchStations() would immediately reload SPIFFS
  // and selectedCountry would jump back to Local.
  if (selectedGenre == "local") selectedGenre = "all";
  previewTag = selectedGenre;
  usingLocalStations = false;

  loadStationsPage(0);
}

void handleEvent(uint8_t ev) {
  browseLastAction = millis();
  lastUserActionMs = millis();
  backlightDimmed = false;
  setBacklight(BACKLIGHT_FULL);

  if (uiMode == MODE_NORMAL) {

    if (ev == EV_CW) audio.setVolume(min(VOL_MAX, audio.getVolume() + 1));
    else if (ev == EV_CCW) audio.setVolume(max(0, audio.getVolume() - 1));
    else if (ev == EV_PRESS) {
      muted = !muted;
      if (muted) {
        lastVol = audio.getVolume();
        audio.setVolume(0);
      } else audio.setVolume(lastVol);
    } else if (ev == EV_LONG) {
      uiMode = MODE_BROWSE;
      focusIndex = F_NOWPLAYING;
      previewStation = currentStation;
      previewTag = selectedGenre;
      previewCountry = selectedCountry;
    }

  } else if (uiMode == MODE_BROWSE) {

    if (ev == EV_CW) focusIndex = (focusIndex + 1) % 3;
    else if (ev == EV_CCW) focusIndex = (focusIndex + 2) % 3;
    else if (ev == EV_PRESS) uiMode = MODE_EDIT;
    else if (ev == EV_LONG) uiMode = MODE_NORMAL;

  } else if (uiMode == MODE_EDIT) {

    // ── F_NOWPLAYING — scroll through loaded stations ─────
    if (focusIndex == F_NOWPLAYING) {
      if (ev == EV_CW && stationCount > 0)
        previewStation = (previewStation + 1) % stationCount;
      else if (ev == EV_CCW && stationCount > 0)
        previewStation = (previewStation - 1 + stationCount) % stationCount;
      else if (ev == EV_PRESS) {
        playStation(previewStation);
        uiMode = MODE_BROWSE;
      } else if (ev == EV_LONG) {
        nextStationsPage();
        uiMode = MODE_EDIT;
        focusIndex = F_NOWPLAYING;
      }
    }

    // ── F_COUNTRY — cycles through COUNTRIES[] table ──────
    else if (focusIndex == F_COUNTRY) {
      if (ev == EV_CW || ev == EV_CCW) {
        int ci = 0;
        for (int i = 0; i < COUNTRY_COUNT; i++) {
          if (previewCountry == COUNTRIES[i].code) {
            ci = i;
            break;
          }
        }
        int dir = (ev == EV_CW) ? 1 : -1;
        ci = (ci + dir + COUNTRY_COUNT) % COUNTRY_COUNT;
        previewCountry = COUNTRIES[ci].code;
      } else if (ev == EV_PRESS) {
        selectedCountry = previewCountry;
        // Local is only the selected source/country. When selecting an online country,
        // clear Genre=Local too, otherwise it would force SPIFFS again.
        if (selectedCountry != "local" && selectedGenre == "local") {
          selectedGenre = "all";
          previewTag = "all";
        }
        Serial.print("Country selected: ");
        Serial.println(selectedCountry);
        applyChange();
        uiMode = MODE_BROWSE;
      } else if (ev == EV_LONG) uiMode = MODE_BROWSE;
    }

    // ── F_TYPE — cycles through GENRES[] table ────────────
    else if (focusIndex == F_TYPE) {
      if (ev == EV_CW || ev == EV_CCW) {
        int gi = 0;
        for (int i = 0; i < GENRE_COUNT; i++) {
          if (previewTag == GENRES[i].tag) {
            gi = i;
            break;
          }
        }
        int dir = (ev == EV_CW) ? 1 : -1;
        gi = (gi + dir + GENRE_COUNT) % GENRE_COUNT;
        previewTag = GENRES[gi].tag;
      } else if (ev == EV_PRESS) {
        selectedGenre = previewTag;
        searchTag = selectedGenre;
        if (selectedGenre == "local") {
          selectedCountry = "local";
          previewCountry = "local";
        }
        applyChange();
        uiMode = MODE_BROWSE;
      } else if (ev == EV_LONG) uiMode = MODE_BROWSE;
    }
  }

  uiDirty = true;
}

void taskRotary(void* p) {
  // T-Display S3 – 2 nyomógomb enkóder helyett:
  //   BTN_BOOT (GPIO0)  rövid (<700ms) = EV_PRESS  (némítás / OK / kiválaszt)
  //   BTN_BOOT (GPIO0)  hosszú         = EV_LONG   (böngésző mód be/ki)
  //   BTN_KEY  (GPIO14) rövid (<700ms) = EV_CW     (következő állomás / fel)
  //   BTN_KEY  (GPIO14) hosszú         = EV_CCW    (előző állomás / le)
  //
  // Hosszú nyomás auto-repeat: BTN_KEY hosszan tartva gyorsan lépteti
  // az állomásokat (450 ms után indul, 65–220 ms ismétlési idő).

  const uint32_t LONG_PRESS_MS      = 700;
  const uint32_t REPEAT_START_MS    = 450;
  const uint32_t REPEAT_MIN_MS      = 65;
  const uint32_t REPEAT_MAX_MS      = 220;

  pinMode(BTN_BOOT, INPUT_PULLUP);
  pinMode(BTN_KEY,  INPUT_PULLUP);

  bool lastBoot = HIGH;
  bool lastKey  = HIGH;
  uint32_t bootPressAt = 0;
  uint32_t keyPressAt  = 0;
  bool bootHeld = false;
  bool keyHeld  = false;
  uint32_t repeatLastMs = 0;

  for (;;) {
    uint32_t now = millis();

    // ── BTN_BOOT ──────────────────────────────────────────────────────────
    bool boot = (bool)digitalRead(BTN_BOOT);
    if (boot == LOW && lastBoot == HIGH) {
      bootPressAt = now;
      holdStartMs = now;
      buttonHolding = true;
      bootHeld = false;
    }
    if (boot == LOW && !bootHeld && (now - bootPressAt >= LONG_PRESS_MS)) {
      bootHeld = true;
      buttonHolding = false;
      uint8_t e = EV_LONG;
      xQueueSend(encQueue, &e, 0);
    }
    if (boot == HIGH && lastBoot == LOW) {
      buttonHolding = false;
      if (!bootHeld) {
        uint8_t e = EV_PRESS;
        xQueueSend(encQueue, &e, 0);
      }
      bootHeld = false;
    }
    lastBoot = boot;

    // ── BTN_KEY ───────────────────────────────────────────────────────────
    bool key = (bool)digitalRead(BTN_KEY);
    if (key == LOW && lastKey == HIGH) {
      keyPressAt   = now;
      keyHeld      = false;
      repeatLastMs = now;
    }
    if (key == LOW) {
      uint32_t held = now - keyPressAt;
      if (!keyHeld && held >= LONG_PRESS_MS) {
        // Első hosszú esemény: EV_CCW (előző)
        keyHeld = true;
        uint8_t e = EV_CCW;
        xQueueSend(encQueue, &e, 0);
        repeatLastMs = now;
      } else if (keyHeld) {
        // Auto-repeat – gyorsuló léptetés
        uint32_t accel = min<uint32_t>(held - LONG_PRESS_MS - REPEAT_START_MS, 1500UL);
        uint32_t interval = (held - LONG_PRESS_MS < REPEAT_START_MS)
                              ? REPEAT_MAX_MS
                              : REPEAT_MAX_MS - ((REPEAT_MAX_MS - REPEAT_MIN_MS) * accel / 1500UL);
        if (now - repeatLastMs >= interval) {
          uint8_t e = EV_CCW;
          xQueueSend(encQueue, &e, 0);
          repeatLastMs = now;
        }
      }
    }
    if (key == HIGH && lastKey == LOW) {
      if (!keyHeld) {
        // Rövid nyomás: EV_CW (következő)
        uint8_t e = EV_CW;
        xQueueSend(encQueue, &e, 0);
      }
      keyHeld = false;
    }
    lastKey = key;

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}


void my_audio_info(Audio::msg_t m) {
  const char* msg = m.msg ? m.msg : "";

  switch (m.e) {
    case Audio::evt_streamtitle:
      Serial.print("[META cb streamtitle] ");
      Serial.println(msg);
      streamTitle = stripIcyStreamTitle(String(msg));
      setMetadataFromStreamTitle(String(msg));
      uiDirty = true;
      break;

    case Audio::evt_id3data:
      Serial.print("[META cb id3] ");
      Serial.println(msg);
      setMetadataFromId3Line(String(msg));
      uiDirty = true;
      break;

    case Audio::evt_name:
      Serial.print("[META cb station] ");
      Serial.println(msg);
      updateCodecFromText(String(msg));
      break;

    case Audio::evt_icydescription:
      Serial.print("[AUDIO cb icydescription] ");
      Serial.println(msg);
      updateCodecFromText(String(msg));
      break;

    case Audio::evt_info: {
      Serial.print("[AUDIO cb info] ");
      Serial.println(msg);
      String s = String(msg);
      String sl = s;
      sl.toLowerCase();
      if (sl.indexOf("bitrate") >= 0 || sl.indexOf("bit rate") >= 0 || sl.indexOf("samplerate") >= 0 || sl.indexOf("sample rate") >= 0) updateBitrateFromText(s);
      updateCodecFromText(s);
      bool parsedKv = setMetadataFromKeyValueLine(s);
      if (!parsedKv && looksLikeRealMetadata(s)) {
        setMetadataFromStreamTitle(s);
        uiDirty = true;
      }
      break;
    }

    case Audio::evt_bitrate:
      Serial.print("[AUDIO cb bitrate] ");
      Serial.println(msg);
      updateBitrateFromText(String(msg));
      break;

    case Audio::evt_icyurl:
      Serial.print("[AUDIO cb icyurl] ");
      Serial.println(msg);
      break;

    case Audio::evt_lasthost:
      Serial.print("[AUDIO cb lasthost] ");
      Serial.println(msg);
      break;

    default:
      // Keep this quiet enough not to flood the serial line while still showing
      // useful events during metadata testing.
      break;
  }
}

void runWifiSetupOnlyLoop() {
  Serial.println("WiFi setup mode locked: AP + webserver only. Radio/audio will not start.");
  uint32_t lastApRetryMs = 0;
  uint32_t lastUiMs = 0;

  while (true) {
    handleSerialMaintenance();
    server.handleClient();

    // If the AP failed to start for any reason, keep retrying instead of falling through to radio mode.
    if (WiFi.softAPgetStationNum() >= 0 && (WiFi.getMode() & WIFI_AP) == 0 && millis() - lastApRetryMs > 5000UL) {
      lastApRetryMs = millis();
      Serial.println("Setup AP not active, retrying...");
      startWifiSetupAP();
    }

    if (millis() - lastUiMs > 1000UL) {
      lastUiMs = millis();
      streamTitle = "Setup AP: SmartWebRadio-Setup";
      deviceIpText = WiFi.softAPIP().toString();
      uiDirty = true;
    }

    if (uiDirty) drawUI();
    delay(5);
    yield();
  }
}


void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  delay(200);

  // BOOT gombot nyomva tartva resetkor: tiszta, csak-soros karbantartó mód.
  // BOOT gombos resetkor is indul; Partition Manager parancsnál helyben vált karbantartó módra.
  pinMode(BTN_BOOT, INPUT_PULLUP);
  if (mrspifsBootMagic == MRSPIFS_BOOT_MAGIC || digitalRead(BTN_BOOT) == LOW) {
    enterSerialMaintenanceMode();
  }
  // T-Display S3: GPIO15 = kijelző tápkapcsoló (HIGH = be kell kapcsolni)
  // GPIO38 = háttérvilágítás PWM
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  pinMode(38, OUTPUT);
  setBacklight(BACKLIGHT_FULL);

  // Single, clean Lovyan init path for the selected display profile.
  tft.init();
  tft.invertDisplay(SMART_WEBRADIO_DISPLAY_INVERT);
#if SMART_WEBRADIO_DISPLAY_320X240
  tft.setRotation(3);   // T-Display S3: landscape, helyes irány
#else
  tft.setRotation(3);
#endif
  tft.setTextWrap(false);
#if SMART_WEBRADIO_DISPLAY_320X240
  if (ESP.getFreePsram() > 0) {
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    Serial.println("[UI320] Canvas: 16-bit PSRAM sprite.");
  } else {
    canvas.setColorDepth(8);
    Serial.println("[UI320] Canvas: 8-bit internal sprite; PSRAM not available.");
  }
#else
  canvas.setColorDepth(16);
#endif
  canvas.createSprite(UI_SCREEN_W, UI_SCREEN_H);
  canvas.setTextWrap(false);
#if !SMART_WEBRADIO_DISPLAY_320X240
  infoLineCanvas.setColorDepth(16);
  infoLineCanvas.createSprite(144, 18);
  infoLineCanvas.setTextWrap(false);
  stationSelectCanvas.setColorDepth(16);
  stationSelectCanvas.createSprite(124, 16);
  stationSelectCanvas.setTextWrap(false);
#endif
  lastUserActionMs = millis();

  bool spiffsOk = SPIFFS.begin(true);
  if (!spiffsOk) {
    Serial.println("SPIFFS mount failed.");
  } else {
    Serial.println("SPIFFS mounted.");
    Serial.print("SPIFFS total: ");
    Serial.print(SPIFFS.totalBytes());
    Serial.print(" bytes, used: ");
    Serial.println(SPIFFS.usedBytes());
#if SMART_WEBRADIO_DISPLAY_320X240 && SMART_WEBRADIO_320_USE_VLW_FONTS
    ui320BeginFonts();
#elif SMART_WEBRADIO_DISPLAY_320X240
    Serial.println("[UI320] VLW font cache disabled for bring-up.");
#else
    ui160BeginFonts();
#endif
  }

  DISP.fillScreen(0x0000);
#if SMART_WEBRADIO_DISPLAY_320X240
  ui320DrawBoot(canvas, tft, "Connecting WiFi...", "", "");
#else
  drawUi160Text(UI160_FONT_BOLD, 8, 18, "Connecting WiFi...", ST77XX_WHITE);
#endif
  canvas.pushSprite(0, 0);

  connectConfiguredWiFi(15000UL);

  DISP.fillScreen(0x0000);
#if SMART_WEBRADIO_DISPLAY_320X240
  ui320DrawBoot(canvas, tft, WiFi.status() == WL_CONNECTED ? "WiFi OK" : "Setup AP active", deviceIpText, spiffsOk ? "SPIFFS OK" : "SPIFFS failed");
#else
  drawUi160Text(UI160_FONT_BOLD, 8, 18, WiFi.status() == WL_CONNECTED ? "WiFi OK" : "Setup AP active", WiFi.status() == WL_CONNECTED ? ST77XX_GREEN : ST77XX_YELLOW);
  drawUi160Text(UI160_FONT_BOLD, 8, 38, deviceIpText, ST77XX_WHITE);
  drawUi160Text(UI160_FONT_BOLD, 8, 58, spiffsOk ? "SPIFFS OK" : "SPIFFS failed", spiffsOk ? ST77XX_WHITE : ST77XX_YELLOW);
#endif
  canvas.pushSprite(0, 0);
  startWebServer();

  if (wifiSetupModeActive || WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi setup mode active: web server only, radio/audio start skipped.");
    streamTitle = "Setup AP: SmartWebRadio-Setup";
    deviceIpText = WiFi.softAPIP().toString();
    uiDirty = true;
    runWifiSetupOnlyLoop();  // never fall through to radio/audio without network
  }

  // ESP32-audioI2S 3.x uses this callback path for stream title / ID3 / ICY data.
  // The old global audio_show... functions remain below as fallback for older builds.
  Audio::audio_info_callback = my_audio_info;

  Serial.print("Heap before audio init: ");
  Serial.print(ESP.getFreeHeap());
#if defined(ARDUINO_ARCH_ESP32)
  Serial.print(" | PSRAM: ");
  Serial.print(ESP.getFreePsram());
#endif
  Serial.println();

  audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
  audio.setVolume(INITIAL_VOLUME);

  encQueue = xQueueCreate(16, 1);
#if SMART_WEBRADIO_ENABLE_ENCODER
  xTaskCreatePinnedToCore(taskRotary, "rotary", 4096, nullptr, 1, nullptr, 1);
#else
  Serial.println("Encoder task disabled for serial SPIFFS maintenance.");
#endif

  loadStartupPrefs();
  if (loadStationsFromSPIFFS()) {
    streamTitle = "Local stations";
    previewStation = 0;
    playStation(0);
  } else if (WiFi.status() != WL_CONNECTED) {
    streamTitle = "Upload stations.txt";
    uiDirty = true;
  } else {
    loadStationsPage(restoreLastStationPending ? lastStationOffset : 0);
  }
}

void loop() {
  handleSerialMaintenance();

  if (mrspifsWriting) {
    delay(1);
    yield();
    return;
  }

  server.handleClient();

  if (wifiSetupModeActive || WiFi.status() != WL_CONNECTED) {
    if (uiDirty) drawUI();
    delay(5);
    yield();
    return;
  }

  audio.loop();
  refreshRuntimeAudioStatus();
  uint8_t ev;
  while (xQueueReceive(encQueue, &ev, 0) == pdTRUE) handleEvent(ev);

  if ((uiMode == MODE_BROWSE || uiMode == MODE_EDIT) && millis() - browseLastAction > 10000) {
    uiMode = MODE_NORMAL;
    uiDirty = true;
  }

  updateBacklight();

  if (buttonHolding) uiDirty = true;  // kor-animacio: folyamatos ujrarajzolas lenyomas kozben

  if (uiMode == MODE_EDIT && focusIndex == F_NOWPLAYING && stationCount > 0) {
    bool stationNameScrolls =
#if SMART_WEBRADIO_DISPLAY_320X240
      ui320BrowseTextWidth(asciiText(stations[previewStation].name)) > 192;
#else
      stations[previewStation].name.length() * 6 > 104;
#endif
    if (stationNameScrolls && millis() - lastScrollFrameMs >= STATION_SCROLL_STEP_MS) {
      lastScrollFrameMs = millis();
      uiDirty = true;
    }
  }

  uint32_t infoFrameStepMs =
#if SMART_WEBRADIO_DISPLAY_320X240
    120UL;
#else
    INFO_SCROLL_STEP_MS;
#endif
  if (uiMode == MODE_NORMAL && infoScrollLine >= 0 && millis() - lastScrollFrameMs >= infoFrameStepMs) {
    lastScrollFrameMs = millis();
    uiDirty = true;
  }

  if (uiDirty) drawUI();
}

void audio_showstreamtitle(const char* info) {
  Serial.print("[META streamtitle] ");
  Serial.println(info ? info : "");
  streamTitle = stripIcyStreamTitle(String(info ? info : ""));
  setMetadataFromStreamTitle(String(info ? info : ""));
  uiDirty = true;
}

void audio_id3data(const char* info) {
  setMetadataFromId3Line(String(info ? info : ""));
}

void audio_showstation(const char* info) {
  // Station name sent by the stream. Do not overwrite our selected station name,
  // only print it for diagnostics.
  Serial.print("[META station] ");
  Serial.println(info ? info : "");
}

void audio_info(const char* info) {
  // Most audio_info lines are diagnostics, but some library/stream combinations
  // expose ICY metadata here. Parse only lines that look like real metadata.
  String s = String(info ? info : "");
  Serial.print("[AUDIO info] ");
  Serial.println(s);
  String sl = s;
  sl.toLowerCase();
  if (sl.indexOf("bitrate") >= 0 || sl.indexOf("bit rate") >= 0 || sl.indexOf("samplerate") >= 0 || sl.indexOf("sample rate") >= 0) updateBitrateFromText(s);
  updateCodecFromText(s);
  if (looksLikeRealMetadata(s)) {
    setMetadataFromStreamTitle(s);
    uiDirty = true;
  }
}
