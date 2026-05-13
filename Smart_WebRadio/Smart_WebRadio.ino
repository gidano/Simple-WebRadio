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
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <mbedtls/base64.h>

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
#define INITIAL_VOLUME 8
#define SERIAL_BAUDRATE 460800

#define STATIONS_FILE "/stations.txt"
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
// Small off-screen line buffer for clipped metadata text.
// This prevents long scrolled artist/title text from wrapping or bleeding
// outside the intended 144 px text area on the ST7735.
GFXcanvas16 infoLineCanvas(144, 12);
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
void handleSerialMaintenance();
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

void handleRoot() {
  String html = "<!doctype html><html><head><meta charset='utf-8'><title>Smart WebRadio</title>";
  html += "<style>body{font-family:Arial;margin:24px;background:#111;color:#eee}textarea{width:100%;height:45vh}button{padding:10px 16px}a{color:#7df}</style></head><body>";
  html += "<h2>Smart WebRadio SPIFFS test</h2>";
  html += "<p>IP: <b>" + deviceIpText + "</b></p>";
  html += "<p><a href='/api/stations'>Download /stations.txt</a></p>";
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

void mrspifsAbortWrite(bool removePartial = true) {
  if (mrspifsWriteFile) mrspifsWriteFile.close();
  if (removePartial && mrspifsWriting && mrspifsWritePath.length()) SPIFFS.remove(mrspifsWritePath);
  mrspifsWriting = false;
  mrspifsWritePath = "";
  mrspifsExpectedSize = 0;
  mrspifsWrittenSize = 0;
}

void handleSerialMaintenanceLine(String line) {
  line.trim();
  if (!line.length()) return;

  if (line == "REBOOT_MAINT") { mrspifsSend("OK|REBOOT_MAINT"); return; }
  if (line == "HELLO") { mrspifsSend("OK|HELLO"); return; }
  if (line == "BEGIN" || line == "MRSPIFS:BEGIN") { mrspifsSend("OK|BEGIN"); return; }
  if (line == "PING") { mrspifsSend("OK|PING"); return; }
  if (line == "LIST") { mrspifsListDir("/"); return; }
  if (line == "WRITE_ABORT") { mrspifsAbortWrite(); mrspifsSend("OK|WRITE_ABORT"); return; }
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
    int sizeSep = line.lastIndexOf('|');
    if (sizeSep <= 17) { mrspifsSend("ERR|WRITE_BEGIN|bad_args"); return; }
    mrspifsWritePath = normalizeFsPath(line.substring(17, sizeSep));
    mrspifsExpectedSize = (size_t)line.substring(sizeSep + 1).toInt();

    audio.stopSong();
    delay(50);
    if (SPIFFS.exists(mrspifsWritePath)) SPIFFS.remove(mrspifsWritePath);
    mrspifsWriteFile = SPIFFS.open(mrspifsWritePath, FILE_WRITE);
    if (!mrspifsWriteFile) { mrspifsSend("ERR|WRITE_BEGIN|open_failed"); return; }

    mrspifsWrittenSize = 0;
    mrspifsWriting = true;
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
    if (w != decodedLen) { mrspifsSend("ERR|WRITE_DATA|write_failed"); return; }

    mrspifsWrittenSize += w;
    mrspifsSend("OK|WRITE_DATA");
    return;
  }

  if (line == "WRITE_END") {
    if (!mrspifsWriting || !mrspifsWriteFile) { mrspifsSend("ERR|WRITE_END|not_open"); return; }
    mrspifsWriteFile.flush();
    mrspifsWriteFile.close();

    bool sizeOk = (mrspifsExpectedSize == 0 || mrspifsExpectedSize == mrspifsWrittenSize);
    String finishedPath = mrspifsWritePath;
    mrspifsAbortWrite(false);

    if (!sizeOk) { SPIFFS.remove(finishedPath); mrspifsSend("ERR|WRITE_END|size_mismatch"); return; }

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
    bool ok = SPIFFS.exists(path) ? SPIFFS.remove(path) : false;
    mrspifsSend(ok ? "OK|DELETE" : "ERR|DELETE|not_found");
    return;
  }

  // SPIFFS is flat, but the PC tool may send these when uploading folders.
  // Accept them for compatibility so the queue does not stop.
  if (line.startsWith("MKDIR|PATH|")) { mrspifsSend("OK|MKDIR"); return; }
  if (line.startsWith("RMDIR|PATH|")) { mrspifsSend("OK|RMDIR"); return; }

  mrspifsSend("ERR|UNKNOWN|" + line);
}

void handleSerialMaintenance() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleSerialMaintenanceLine(line);
      line = "";
    } else {
      if (line.length() < 900) line += c;
      else line = "";
    }
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

int statusChipWidth(const String& txt) {
  // Slightly tighter than the outer chips so labels like "1.4M FLAC" fit
  // between the left country and right genre boxes on the 160 px display.
  return txt.length() * 6 + 6;
}

void statusChip(int x, int y, String txt, uint16_t col) {
  int w = statusChipWidth(txt);
  DISP.drawRoundRect(x, y, w, 14, 3, col);
  DISP.setTextColor(col);
  DISP.setCursor(x + 3, y + 4);
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


int textPixelWidth(const String& text) {
  return text.length() * 6;
}

void drawClippedInfoText(int x, int y, int w, const String& text, int offset, uint16_t color, bool centered) {
  const int charW = 6;
  const int lineW = 144;
  const int lineH = 12;
  uint16_t cardBg = tft.color565(32, 73, 93);

  // The metadata box is fixed at 144 px in this layout. If a future tweak calls
  // this with another width, keep the drawing safe instead of corrupting rows.
  if (w != lineW) w = lineW;

  infoLineCanvas.setTextWrap(false);
  infoLineCanvas.fillScreen(cardBg);
  infoLineCanvas.setTextColor(color);

  int tx = 0;
  if (centered) {
    tx = (w - textPixelWidth(text)) / 2;
  } else {
    tx = -offset;
  }

  infoLineCanvas.setCursor(tx, 1);
  infoLineCanvas.print(text);

  DISP.drawRGBBitmap(x, y, infoLineCanvas.getBuffer(), w, lineH);
}

bool infoLineLong(int line, const String& stationLine, const String& artistLine, const String& titleLine, int w) {
  if (line == INFO_LINE_STATION) return textPixelWidth(stationLine) > w;
  if (line == INFO_LINE_ARTIST)  return textPixelWidth(artistLine) > w;
  if (line == INFO_LINE_TITLE)   return textPixelWidth(titleLine) > w;
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
  int maxOffset = textPixelWidth(activeText) - w + 6;
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
    drawClippedInfoText(x, y, w, "", 0, color, true);
    return;
  }

  int textW = textPixelWidth(text);
  if (textW <= w) {
    drawClippedInfoText(x, y, w, text, 0, color, true);
    return;
  }

  int offset = 0;
  if (line == infoScrollLine) {
    int maxOffset = textW - w + 6;
    uint32_t runMs = (uint32_t)maxOffset * INFO_SCROLL_STEP_MS;
    uint32_t t = millis() - infoScrollStartMs;
    offset = (t < runMs) ? (int)(t / INFO_SCROLL_STEP_MS) : 0;
  }

  drawClippedInfoText(x, y, w, text, offset, color, false);
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
  DISP.setCursor(8, 6);
  DISP.setTextColor(accent);
  DISP.print("Volume");

  // volume bars - pulled closer to the Volume label to make room
  // for the long-press progress indicator at the right end of the top row.
  uint16_t volCol = (uiMode == MODE_NORMAL) ? accent : 0x39E7;
  const int volBarsX = 54;
  const int volBarsY = 6;
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

    DISP.setTextColor(npCol);
    DISP.setCursor((160 - 7 * 6) / 2, 30);
    DISP.print("Station");

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

    printInfoTextLine(INFO_LINE_STATION, infoX, 30, infoW, stationLine, npCol);
    printInfoTextLine(INFO_LINE_ARTIST,  infoX, 48, infoW, artistLine, ST77XX_CYAN);
    printInfoTextLine(INFO_LINE_TITLE,   infoX, 62, infoW, titleLine, ST77XX_WHITE);
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
  int cChipW = cLabel.length() * 6 + 10;
  chip(4, 80, cLabel, focusIndex == F_COUNTRY ? warn : accent);

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
  int gChipW = gLabel.length() * 6 + 10;
  int gChipX = max(4, 156 - gChipW);  // right aligned with 4px margin

  // ── Bitrate / codec status chip, centered in the free space between country and genre ──
  int leftChipRight = 4 + cChipW;
  int rightChipLeft = gChipX;
  int middleCenter = (leftChipRight + rightChipLeft) / 2;

  String aLabel = audioStatusLabel(false);  // e.g. "128k MP3" or "1.4M FLAC"
  int aChipW = statusChipWidth(aLabel);
  int aChipX = middleCenter - aChipW / 2;
  if (leftChipRight + 2 <= aChipX && aChipX + aChipW + 2 <= rightChipLeft) {
    statusChip(aChipX, 80, aLabel, ST77XX_WHITE);
  } else {
    // Very long country / genre labels can leave no middle space. Keep the UI clean.
    aLabel = audioStatusLabel(true);   // shorter: "128k" / "1.4M"
    aChipW = statusChipWidth(aLabel);
    aChipX = middleCenter - aChipW / 2;
    if (leftChipRight + 2 <= aChipX && aChipX + aChipW + 2 <= rightChipLeft) {
      statusChip(aChipX, 80, aLabel, ST77XX_WHITE);
    }
  }

  chip(gChipX, 80, gLabel, focusIndex == F_TYPE ? warn : ST77XX_CYAN);

  // bottom hint
  DISP.setCursor(8, 112);
  DISP.setTextColor(ST77XX_WHITE);
  if (uiMode == MODE_NORMAL) DISP.print("Smart WebRadio by gidano");
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
  // Encoder es 3 gomb egyszerre tamogatva, ugyanazon pineken:
  //   ENC_CLK (pin 11) = BTN_NEXT  -> EV_CW
  //   ENC_DT  (pin 10) = BTN_PREV  -> EV_CCW
  //   ENC_SW  (pin  6) = BTN_OK    -> EV_PRESS / EV_LONG
  //
  // Extra: BTN_NEXT / BTN_PREV hosszan nyomva automatikusan ismetel.
  // Allomaskeresesnel igy nem kell egyesevel leptetni: minel tovabb tartod,
  // annal gyorsabban jonnek az EV_CW / EV_CCW lepesek.
  //
  // Fontos: encoder-forgasnal nem indul be az auto-repeat, csak akkor,
  // ha az adott pin tenyleg lent marad, es a masik pin nem jelez forgast.

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);

  const uint32_t BTN_REPEAT_START_MS = 450;  // ennyi ido utan indul a gyors leptetes
  const uint32_t BTN_REPEAT_MIN_MS   = 65;   // leggyorsabb ismetles
  const uint32_t BTN_REPEAT_MAX_MS   = 220;  // kezdeti ismetles

  int  lastClk    = digitalRead(ENC_CLK);
  int  lastDt     = digitalRead(ENC_DT);
  bool lastBtn    = HIGH;
  uint32_t pressAt   = 0;
  bool     dtPending = false;
  uint32_t dtPendMs  = 0;

  // Auto-repeat allapot a + / - gombokra.
  // repeatDir:  1 = NEXT / EV_CW, -1 = PREV / EV_CCW, 0 = nincs
  int repeatDir = 0;
  uint32_t repeatStartMs = 0;
  uint32_t repeatLastMs  = 0;

  auto stopRepeat = [&]() {
    repeatDir = 0;
    repeatStartMs = 0;
    repeatLastMs = 0;
  };

  auto startRepeat = [&](int dir) {
    repeatDir = dir;
    repeatStartMs = millis();
    repeatLastMs = repeatStartMs;
  };

  auto serviceRepeat = [&]() {
    if (repeatDir == 0) return;

    uint32_t now = millis();
    uint32_t held = now - repeatStartMs;
    if (held < BTN_REPEAT_START_MS) return;

    // Fokozatos gyorsitas: 220ms -> 65ms kb. masfel masodperc alatt.
    uint32_t accel = min<uint32_t>(held - BTN_REPEAT_START_MS, 1500UL);
    uint32_t interval = BTN_REPEAT_MAX_MS - ((BTN_REPEAT_MAX_MS - BTN_REPEAT_MIN_MS) * accel / 1500UL);

    if (now - repeatLastMs >= interval) {
      uint8_t e = (repeatDir > 0) ? EV_CW : EV_CCW;
      xQueueSend(encQueue, &e, 0);
      repeatLastMs = now;
    }
  };

  for (;;) {
    int clk = digitalRead(ENC_CLK);
    int dt  = digitalRead(ENC_DT);

    // Ha mindketto fent van, nincs + / - gomb lenyomva.
    if (clk == HIGH && dt == HIGH) {
      stopRepeat();
    }

    // --- DT leeso ele: lehet encoder CCW elofutara VAGY BTN_PREV gomb ---
    if (dt != lastDt && dt == LOW) {
      dtPending = true;
      dtPendMs  = millis();
      stopRepeat();
    }

    // --- Encoder CW / CCW (CLK leeso ele) ---
    if (clk != lastClk && clk == LOW) {
      dtPending = false;           // CLK megerositette: encoder forgott, nem gomb
      stopRepeat();
      uint8_t e = dt ? EV_CW : EV_CCW;
      xQueueSend(encQueue, &e, 0);

      // Ha a CLK lent marad es DT fent van, ez nem forgatas, hanem NEXT gomb tartasa.
      // Az elso lepes mar kiment, innentol csak auto-repeat jon kesleltetve.
      if (dt == HIGH) {
        startRepeat(1);
      }
    }

    // --- BTN_PREV: DT volt lent 30ms-ig, de CLK nem esett le -> gombnyomas ---
    if (dtPending && (millis() - dtPendMs >= 30)) {
      dtPending = false;
      uint8_t e = EV_CCW;
      xQueueSend(encQueue, &e, 0);
      startRepeat(-1);
    }

    // Ha kozben encoder jellegu kombinacio lesz, alljon le a gomb-repeat.
    if (repeatDir > 0 && !(clk == LOW && dt == HIGH)) stopRepeat();
    if (repeatDir < 0 && !(dt == LOW && clk == HIGH)) stopRepeat();
    serviceRepeat();

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

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  delay(200);
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initBlue();
  tft.setRotation(1);
  tft.setTextWrap(false);
  canvas.setTextWrap(false);
#if TFT_BL >= 0
  pinMode(TFT_BL, OUTPUT);
  setBacklight(BACKLIGHT_FULL);
#endif
  lastUserActionMs = millis();

  DISP.fillScreen(0x0000);
  DISP.setTextColor(ST77XX_WHITE);
  DISP.setCursor(8, 20);
  DISP.print("Connecting WiFi...");
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(300);
  deviceIpText = WiFi.localIP().toString();
  Serial.print("WiFi IP: ");
  Serial.println(deviceIpText);

  DISP.fillScreen(0x0000);
  DISP.setTextColor(ST77XX_GREEN);
  DISP.setCursor(8, 20);
  DISP.print("WiFi OK");
  DISP.setTextColor(ST77XX_WHITE);
  DISP.setCursor(8, 40);
  DISP.print(deviceIpText);
  DISP.setCursor(8, 60);
  DISP.print("SPIFFS init...");
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 128);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed.");
  } else {
    Serial.println("SPIFFS mounted.");
    Serial.print("SPIFFS total: ");
    Serial.print(SPIFFS.totalBytes());
    Serial.print(" bytes, used: ");
    Serial.println(SPIFFS.usedBytes());
  }
  startWebServer();

  // ESP32-audioI2S 3.x uses this callback path for stream title / ID3 / ICY data.
  // The old global audio_show... functions remain below as fallback for older builds.
  Audio::audio_info_callback = my_audio_info;

  audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
  audio.setVolume(INITIAL_VOLUME);

  encQueue = xQueueCreate(16, 1);
  xTaskCreatePinnedToCore(taskRotary, "rotary", 4096, nullptr, 1, nullptr, 1);

  loadStartupPrefs();
  if (loadStationsFromSPIFFS()) {
    streamTitle = "Local stations";
    previewStation = 0;
    playStation(0);
  } else {
    loadStationsPage(restoreLastStationPending ? lastStationOffset : 0);
  }
}

void loop() {
  server.handleClient();
  handleSerialMaintenance();
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
    if (stations[previewStation].name.length() * 6 > 104 && millis() - lastScrollFrameMs >= STATION_SCROLL_STEP_MS) {
      lastScrollFrameMs = millis();
      uiDirty = true;
    }
  }

  if (uiMode == MODE_NORMAL && infoScrollLine >= 0 && millis() - lastScrollFrameMs >= INFO_SCROLL_STEP_MS) {
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
