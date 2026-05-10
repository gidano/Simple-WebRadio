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
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "secrets.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <Preferences.h>

#define I2S_DOUT  7	
#define I2S_BCLK  8
#define I2S_LRCK  9

#define TFT_SCK   1
#define TFT_MOSI  2	//	SDA
#define TFT_DC    3
#define TFT_RST   4
#define TFT_CS    5

// Backlight PWM. Set this to your display BL/LED pin if it is wired to the ESP32.
// Leave -1 if the display backlight is tied directly to VCC and cannot be dimmed.
#define TFT_BL    12
#define BACKLIGHT_FULL      200 //  0-255
#define BACKLIGHT_DIM       50  //  0-255
#define BACKLIGHT_DIM_AFTER 60000UL  // 1 minutes


/* ESP32S3 SUPERMINI */

/*ENC_CLK  (pin 11)  =  BTN_NEXT  →  EV_CW  (hangerő fel / következő)
ENC_DT   (pin 10)  =  BTN_PREV  →  EV_CCW (hangerő le / előző)
ENC_SW   (pin  6)  =  BTN_OK    →  EV_PRESS / EV_LONG	*/

#define ENC_CLK   11	//	43  Seeed XIAO ESP32S3
#define ENC_DT    10	//	44  Seeed XIAO ESP32S3
#define ENC_SW    6   //  6   Seeed XIAO ESP32S3

#define RB_HOST "http://de1.api.radio-browser.info"   // API marad HTTP-n: ESP32-n stabilabb, mint TLS/HTTPS
#define RB_PAGE_SIZE 30
#define RB_API_LIMIT RB_PAGE_SIZE
#define RB_RESULT_LIMIT 35
#define VOL_MAX 21
#define INITIAL_VOLUME 2

// ── Editable country list — add/remove as needed ──────────
// Format: {"CODE", "Display Name"}
// Codes must match Radio Browser countrycode field (ISO 3166-1 alpha-2)
struct CountryEntry {
  const char* code;
  const char* name;
};
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
static const int COUNTRY_COUNT = sizeof(COUNTRIES) / sizeof(COUNTRIES[0]);

// ── Editable genre list ────────────────────────────────────
struct GenreEntry {
  const char* tag;
  const char* label;
};
static const GenreEntry GENRES[] = {
  { "all", "All" },
  { "music", "Music" },
  { "news", "News" },
  { "jazz", "Jazz" },
  { "classical", "Classical" },
  { "rock", "Rock" },
};
static const int GENRE_COUNT = sizeof(GENRES) / sizeof(GENRES[0]);

// Blue panel fix: setColRowStart() is protected, so we access it through a subclass.
// No need to modify the library; initBlue() sets the correct offset
// (2px column + 1px row) while preserving the BLACKTAB color setting.
class ST7735_BlueFix : public Adafruit_ST7735 {
public:
  ST7735_BlueFix(int8_t cs, int8_t dc, int8_t rst)
    : Adafruit_ST7735(cs, dc, rst) {}
  void initBlue() {
    initR(INITR_BLACKTAB);   // Preservation of original colors (MADCTL)
    setColRowStart(0, 0);    // V2 at the blue panel: 2px col + 1px row offset
  }
};

Audio audio;
Preferences prefs;
ST7735_BlueFix tft(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(160, 128);
QueueHandle_t encQueue;

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
  String country;
};

Station stations[RB_RESULT_LIMIT];
int stationCount = 0;
int currentStation = 0;
int focusIndex = 0;
UiMode uiMode = MODE_NORMAL;
bool uiDirty = true;
String streamTitle = "Loading...";
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

// Station selector text scroll: long names run through once, wait 5 seconds, then repeat.
static const uint32_t STATION_SCROLL_PAUSE_MS = 5000UL;
static const uint32_t STATION_SCROLL_STEP_MS  = 45UL;
int scrollLastStation = -1;
String scrollLastText = "";
uint32_t scrollStartMs = 0;
uint32_t lastScrollFrameMs = 0;

volatile uint32_t holdStartMs = 0;
volatile bool buttonHolding = false;

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
  if (lu.indexOf(".mp3") >= 0) return true;
  if (lu.indexOf("aac") >= 0) return true;
  return false;
}

String asciiText(String s) {
  // Keep the display readable with fonts that only contain the English ASCII set.
  // Common accented letters are transliterated; unknown UTF-8 characters are dropped.
  s.replace("á", "a"); s.replace("Á", "A");
  s.replace("é", "e"); s.replace("É", "E");
  s.replace("í", "i"); s.replace("Í", "I");
  s.replace("ó", "o"); s.replace("Ó", "O");
  s.replace("ö", "o"); s.replace("Ö", "O");
  s.replace("ő", "o"); s.replace("Ő", "O");
  s.replace("ú", "u"); s.replace("Ú", "U");
  s.replace("ü", "u"); s.replace("Ü", "U");
  s.replace("ű", "u"); s.replace("Ű", "U");

  s.replace("ä", "a"); s.replace("Ä", "A");
  s.replace("à", "a"); s.replace("À", "A");
  s.replace("â", "a"); s.replace("Â", "A");
  s.replace("å", "a"); s.replace("Å", "A");
  s.replace("ã", "a"); s.replace("Ã", "A");
  s.replace("ç", "c"); s.replace("Ç", "C");
  s.replace("è", "e"); s.replace("È", "E");
  s.replace("ê", "e"); s.replace("Ê", "E");
  s.replace("ë", "e"); s.replace("Ë", "E");
  s.replace("ì", "i"); s.replace("Ì", "I");
  s.replace("î", "i"); s.replace("Î", "I");
  s.replace("ï", "i"); s.replace("Ï", "I");
  s.replace("ñ", "n"); s.replace("Ñ", "N");
  s.replace("ò", "o"); s.replace("Ò", "O");
  s.replace("ô", "o"); s.replace("Ô", "O");
  s.replace("õ", "o"); s.replace("Õ", "O");
  s.replace("ø", "o"); s.replace("Ø", "O");
  s.replace("ù", "u"); s.replace("Ù", "U");
  s.replace("û", "u"); s.replace("Û", "U");
  s.replace("ý", "y"); s.replace("Ý", "Y");
  s.replace("ÿ", "y"); s.replace("Ÿ", "Y");
  s.replace("ß", "ss");
  s.replace("–", "-"); s.replace("—", "-");
  s.replace("’", "'"); s.replace("‘", "'");
  s.replace("“", "\""); s.replace("”", "\"");
  s.replace("…", "...");

  String out;
  out.reserve(s.length());
  for (int i = 0; i < s.length(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 32 && c <= 126) out += (char)c;
  }
  out.trim();
  if (out.length() == 0) out = "Unknown";
  return out;
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
  streamTitle = stations[i].name;
  audio.connecttohost(stations[i].url.c_str());
  saveLastStation(i);
  uiDirty = true;
}

String countryName(String c) {
  c.toUpperCase();
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

void chip(int x, int y, String txt, uint16_t col) {
  int w = txt.length() * 6 + 10;
  bool activeEdit = (uiMode == MODE_EDIT && ((focusIndex == 1 && x < 60) || (focusIndex == 2 && x > 60)));
  if (activeEdit) {
    DISP.fillRoundRect(x, y, w, 14, 3, col);
    DISP.setTextColor(ST77XX_BLACK);
  } else {
    DISP.drawRoundRect(x, y, w, 14, 3, col);
    DISP.setTextColor(col);
  }
  DISP.setCursor(x + 5, y + 4);
  DISP.print(txt);
}


void setBacklight(uint8_t value) {
#if TFT_BL >= 0
  analogWrite(TFT_BL, value);
#endif
}

void updateBacklight() {
#if TFT_BL >= 0
  bool shouldDim = (millis() - lastUserActionMs >= BACKLIGHT_DIM_AFTER);
  if (shouldDim != backlightDimmed) {
    backlightDimmed = shouldDim;
    setBacklight(backlightDimmed ? BACKLIGHT_DIM : BACKLIGHT_FULL);
  }
#endif
}

void resetStationScroll(const String& text, int stationIndex) {
  scrollLastStation = stationIndex;
  scrollLastText = text;
  scrollStartMs = millis();
}

void printScrolledText(int x, int y, int w, const String& text, uint16_t color) {
  const int charW = 6;
  int visibleChars = max(1, w / charW);
  int textW = text.length() * charW;

  DISP.setTextColor(color);

  if (textW <= w) {
    int tx = x + (w - textW) / 2;
    DISP.setCursor(tx, y);
    DISP.print(text);
    return;
  }

  if (text != scrollLastText) resetStationScroll(text, scrollLastStation);

  int maxOffset = textW - w + charW;
  uint32_t runMs = (uint32_t)maxOffset * STATION_SCROLL_STEP_MS;
  uint32_t cycleMs = runMs + STATION_SCROLL_PAUSE_MS;
  uint32_t t = (millis() - scrollStartMs) % cycleMs;
  int offset = (t < runMs) ? (int)(t / STATION_SCROLL_STEP_MS) : 0;

  int startChar = offset / charW;
  int pixelShift = offset % charW;
  String padded = text + "   ";
  String part = padded.substring(startChar, min((int)padded.length(), startChar + visibleChars + 2));

  DISP.setCursor(x - pixelShift, y);
  DISP.print(part);
}

void drawPauseOverlay() {
  if (!muted || uiMode != MODE_NORMAL) return;
  uint16_t panel = tft.color565(35, 35, 45);
  uint16_t edge = ST77XX_YELLOW;
  int x = 52, y = 62, w = 56, h = 22;
  DISP.fillRoundRect(x, y, w, h, 4, panel);
  DISP.drawRoundRect(x, y, w, h, 4, edge);
  DISP.setTextColor(edge);
  DISP.setCursor(x + 13, y + 8);
  DISP.print("PAUSE");
}

void drawUI() {
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
  DISP.setCursor(8, 6);
  DISP.setTextColor(accent);
  DISP.print("Volume");

  // volume bars
  uint16_t volCol = (uiMode == MODE_NORMAL) ? accent : 0x39E7;
  for (int i = 0; i < 12; i++) {
    uint16_t c = (i < audio.getVolume() * 12 / 21) ? volCol : 0x2104;
    DISP.fillRoundRect(90 + i * 5, 6, 4, 6, 1, c);
  }

  // "Now Playing" label
  uint16_t npCol = (focusIndex == F_NOWPLAYING && uiMode != MODE_NORMAL)
                     ? warn
                     : ST77XX_WHITE;
  DISP.setTextColor(npCol);
  DISP.setCursor((160 - 11 * 6) / 2, 30);
  DISP.print("Now Playing");

  // station name / browse preview
  if (uiMode == MODE_EDIT && focusIndex == F_NOWPLAYING) {
    String np = stations[previewStation].name;
    if (scrollLastStation != previewStation || scrollLastText != np) resetStationScroll(np, previewStation);

    DISP.fillRoundRect(12, 44, 136, 16, 3, warn);
    DISP.setTextColor(ST77XX_BLACK);
    DISP.setCursor(18, 48);
    DISP.print("<");
    DISP.setCursor(136, 48);
    DISP.print(">");
    printScrolledText(28, 48, 104, np, ST77XX_BLACK);

    // counter  e.g. "3/15"
    String countTxt = String(previewStation + 1) + "/" + String(stationCount) + "  P" + String(rbPage + 1);
    DISP.setTextColor(warn);
    DISP.setCursor((160 - (int)countTxt.length() * 6) / 2, 66);
    DISP.print(countTxt);
  } else {
    String np = streamTitle.substring(0, 24);
    int npX = (160 - (int)np.length() * 6) / 2;
    DISP.setTextColor(ST77XX_WHITE);
    DISP.setCursor(npX, 48);
    DISP.print(np);
  }

  // ── Country chip ─────────────────────────────────────────
  // Szerkesztés közben az „Előnézet” országot jelenítsd meg, egyébként a „Kiválasztott országot”
  // (visszatér a lejátszó országához, amikor „all”)
  String cCode = (uiMode == MODE_EDIT && focusIndex == F_COUNTRY)
                   ? String(previewCountry)
                   : (selectedCountry == "all"
                        ? (stationCount > 0 ? stations[currentStation].country : String("all"))
                        : String(selectedCountry));
  chip(4, 80, countryName(cCode), focusIndex == F_COUNTRY ? warn : accent);

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
  chip(92, 80, gLabel, focusIndex == F_TYPE ? warn : ST77XX_CYAN);

  // hold-progress circle
  DISP.drawCircle(149, 116, 8, ST77XX_WHITE);
  if (buttonHolding) {
    int prog = min(8, (int)((millis() - holdStartMs) / 87));
    for (int i = 0; i < prog; i++)
      DISP.fillCircle(149, 116, i, accent);
  }

  // bottom hint
  DISP.setCursor(8, 112);
  DISP.setTextColor(ST77XX_WHITE);
  if (uiMode == MODE_NORMAL) DISP.print("Simple WebRadio");
  else if (uiMode == MODE_BROWSE) DISP.print("Rotate Select");
  else if (focusIndex == F_NOWPLAYING) DISP.print("Long=Next Page");
  else DISP.print("Rotate Change");

  drawPauseOverlay();

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128);

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
        applyChange();
        uiMode = MODE_BROWSE;
      } else if (ev == EV_LONG) uiMode = MODE_BROWSE;
    }
  }

  uiDirty = true;
}

void taskRotary(void* p) {
  // Encoder es 3 gomb egyszerre tamogatva, ugyanazon pineken:
  //   ENC_CLK (pin 11) = BTN_NEXT  -> EV_CW
  //   ENC_DT  (pin 10) = BTN_PREV  -> EV_CCW
  //   ENC_SW  (pin  6) = BTN_OK    -> EV_PRESS / EV_LONG
  //
  // BTN_PREV (DT pin) megbizhatosag:
  //   A "CLK==HIGH" feltetel erzekeny a pin lebegese/zajara, ezert
  //   pending-flag + 30ms timeout modszert hasznalunk:
  //     - DT leesik  -> dtPending = true, idopont rogzitese
  //     - CLK leesik -> encoder esemeny, dtPending torles (nem gomb volt)
  //     - 30ms mulva dtPending meg mindig igaz -> biztosan gomb -> EV_CCW

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);

  int  lastClk    = digitalRead(ENC_CLK);
  int  lastDt     = digitalRead(ENC_DT);
  bool lastBtn    = HIGH;
  uint32_t pressAt   = 0;
  bool     dtPending = false;
  uint32_t dtPendMs  = 0;

  for (;;) {
    int clk = digitalRead(ENC_CLK);
    int dt  = digitalRead(ENC_DT);

    // --- DT leeso ele: lehet encoder CCW elofutara VAGY BTN_PREV gomb ---
    if (dt != lastDt && dt == LOW) {
      dtPending = true;
      dtPendMs  = millis();
    }

    // --- Encoder CW / CCW (CLK leeso ele) ---
    if (clk != lastClk && clk == LOW) {
      dtPending = false;           // CLK megerositette: encoder forgott, nem gomb
      uint8_t e = dt ? EV_CW : EV_CCW;
      xQueueSend(encQueue, &e, 0);
    }

    // --- BTN_PREV: DT volt lent 30ms-ig, de CLK nem esett le -> gombnyomas ---
    if (dtPending && (millis() - dtPendMs >= 30)) {
      dtPending = false;
      uint8_t e = EV_CCW;
      xQueueSend(encQueue, &e, 0);
    }

    lastClk = clk;
    lastDt  = dt;

    // --- Encoder gomb / BTN_OK: rovid = EV_PRESS, hosszu = EV_LONG ---
    bool btn = digitalRead(ENC_SW);
    if (btn == LOW && lastBtn == HIGH) {
      pressAt = millis();
      holdStartMs = pressAt;
      buttonHolding = true;
    }
    if (btn == HIGH && lastBtn == LOW) {
      buttonHolding = false;
      uint8_t e = (millis() - pressAt > 700) ? EV_LONG : EV_PRESS;
      xQueueSend(encQueue, &e, 0);
    }
    lastBtn = btn;

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initBlue();
  tft.setRotation(1);
#if TFT_BL >= 0
  pinMode(TFT_BL, OUTPUT);
  setBacklight(BACKLIGHT_FULL);
#endif
  lastUserActionMs = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
  audio.setVolume(INITIAL_VOLUME);

  encQueue = xQueueCreate(16, 1);
  xTaskCreatePinnedToCore(taskRotary, "rotary", 4096, nullptr, 1, nullptr, 1);

  loadStartupPrefs();
  loadStationsPage(restoreLastStationPending ? lastStationOffset : 0);
}

void loop() {
  audio.loop();
  uint8_t ev;
  while (xQueueReceive(encQueue, &ev, 0) == pdTRUE) handleEvent(ev);

  if ((uiMode == MODE_BROWSE || uiMode == MODE_EDIT) && millis() - browseLastAction > 10000) {
    uiMode = MODE_NORMAL;
    uiDirty = true;
  }

  updateBacklight();

  if (buttonHolding) uiDirty = true;  // kor-animacio: folyamatos ujrarajzolas lenyomas kozben

  if (uiMode == MODE_EDIT && focusIndex == F_NOWPLAYING && stationCount > 0) {
    if (stations[previewStation].name.length() * 6 > 104 && millis() - lastScrollFrameMs >= STATION_SCROLL_STEP_MS) {
      lastScrollFrameMs = millis();
      uiDirty = true;
    }
  }

  if (uiDirty) drawUI();
}

void audio_showstreamtitle(const char* info) {
  streamTitle = asciiText(String(info));
  uiDirty = true;
}
