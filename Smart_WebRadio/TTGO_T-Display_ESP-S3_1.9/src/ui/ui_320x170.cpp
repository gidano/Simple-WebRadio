#include "ui_320x170.h"
#include <stdlib.h>

// ── Font útvonalak ────────────────────────────────────────────────────────────

static const char* fontPath(Ui320FontRole role) {
  switch (role) {
    case UI320_FONT_SMALL:     return "/fonts/test_12.vlw";
    case UI320_FONT_NORMAL:    return "/fonts/test_14.vlw";
    case UI320_FONT_BOLD:      return "/fonts/test_sb_17.vlw";
    case UI320_FONT_INFO:      return "/fonts/test_17.vlw";
    case UI320_FONT_INFO_BOLD: return "/fonts/test_sb_17.vlw";
    case UI320_FONT_ARTIST:    return "/fonts/test_sb_20.vlw";
    case UI320_FONT_TITLE:     return "/fonts/test_sb_20.vlw";
    case UI320_FONT_STATION:   return "/fonts/test_sb_20.vlw";
    default:                   return "/fonts/test_14.vlw";
  }
}

// ── Font cache ────────────────────────────────────────────────────────────────

struct Ui320FontCache {
  const char* path;
  uint8_t*    data;
  uint32_t    size;
  bool        tried;
  bool        ok;
  bool        drawFailReported;
};

static Ui320FontCache fontCache[] = {
  { "/fonts/test_12.vlw",    nullptr, 0, false, false, false },
  { "/fonts/test_14.vlw",    nullptr, 0, false, false, false },
  { "/fonts/test_17.vlw",    nullptr, 0, false, false, false },
  { "/fonts/test_sb_17.vlw", nullptr, 0, false, false, false },
  { "/fonts/test_sb_20.vlw", nullptr, 0, false, false, false }
};

static const int FONT_CACHE_COUNT = sizeof(fontCache) / sizeof(fontCache[0]);

static Ui320FontCache* findFontCache(const char* path) {
  for (int i = 0; i < FONT_CACHE_COUNT; i++) {
    if (strcmp(fontCache[i].path, path) == 0) return &fontCache[i];
  }
  return nullptr;
}

static String fallbackRootPath(const char* path) {
  String p(path);
  if (p.startsWith("/fonts/")) return "/" + p.substring(7);
  return p;
}

uint32_t ui320FontFileSize(const char* path) {
  Ui320FontCache* cache = findFontCache(path);
  if (cache && cache->ok) return cache->size;
  File f = SPIFFS.open(path, FILE_READ);
  if (!f) {
    String fb = fallbackRootPath(path);
    if (fb != path) f = SPIFFS.open(fb, FILE_READ);
  }
  bool opened = (bool)f;
  uint32_t size = opened ? (uint32_t)f.size() : 0;
  if (opened) f.close();
  return size;
}

static bool loadFontData(Ui320FontCache* cache) {
  if (!cache) return false;
  if (cache->tried) return cache->ok;
  cache->tried = true;

  File f = SPIFFS.open(cache->path, FILE_READ);
  if (!f) {
    String fb = fallbackRootPath(cache->path);
    if (fb != cache->path) f = SPIFFS.open(fb, FILE_READ);
  }
  if (!f) {
    Serial.print("[UI170 font cache open FAILED] ");
    Serial.println(cache->path);
    return false;
  }

  cache->size = (uint32_t)f.size();
  if (cache->size == 0) {
    f.close();
    Serial.print("[UI170 font cache empty] ");
    Serial.println(cache->path);
    return false;
  }

#if defined(ARDUINO_ARCH_ESP32)
  cache->data = (ESP.getFreePsram() > cache->size + 4096)
                ? (uint8_t*)ps_malloc(cache->size)
                : (uint8_t*)malloc(cache->size);
#else
  cache->data = (uint8_t*)malloc(cache->size);
#endif

  if (!cache->data) {
    f.close();
    Serial.print("[UI170 font cache malloc FAILED] ");
    Serial.print(cache->path);
    Serial.print(" size="); Serial.println(cache->size);
    return false;
  }

  uint32_t readTotal = 0;
  while (readTotal < cache->size) {
    int n = f.read(cache->data + readTotal, cache->size - readTotal);
    if (n <= 0) break;
    readTotal += (uint32_t)n;
    yield();
  }
  f.close();

  if (readTotal != cache->size) {
    free(cache->data);
    cache->data = nullptr;
    cache->size = 0;
    Serial.print("[UI170 font cache read FAILED] ");
    Serial.println(cache->path);
    return false;
  }

  cache->ok = true;
  Serial.print("[UI170 font cached] ");
  Serial.print(cache->path);
  Serial.print(" size="); Serial.print(cache->size);
  Serial.print(" heap="); Serial.print(ESP.getFreeHeap());
#if defined(ARDUINO_ARCH_ESP32)
  Serial.print(" psram="); Serial.print(ESP.getFreePsram());
#endif
  Serial.println();
  return true;
}

static bool loadFontData(const char* path) {
  return loadFontData(findFontCache(path));
}

bool ui320BeginFonts() {
  const Ui320FontRole required[] = {
    UI320_FONT_SMALL, UI320_FONT_NORMAL, UI320_FONT_BOLD,
    UI320_FONT_INFO, UI320_FONT_INFO_BOLD,
    UI320_FONT_ARTIST, UI320_FONT_TITLE, UI320_FONT_STATION
  };

  bool ok = true;
  for (Ui320FontRole role : required) {
    const char* path = fontPath(role);
    uint32_t size = ui320FontFileSize(path);
#if SMART_WEBRADIO_320_CACHE_VLW_FONTS
    bool cached = loadFontData(path);
#else
    bool cached = (size > 0);
#endif
    Serial.print("[UI170 font check] ");
    Serial.print(path);
    Serial.print(" exists="); Serial.print(SPIFFS.exists(path) ? "YES" : "NO");
    Serial.print(" size="); Serial.print(size);
    Serial.print(" cached=");
    Serial.println(cached ? (SMART_WEBRADIO_320_CACHE_VLW_FONTS ? "YES" : "NO, direct SPIFFS") : "NO");
    if (!cached) ok = false;
  }
  return ok;
}

// ── Font betöltés sprite-ba ───────────────────────────────────────────────────

bool ui320UseFontPath(LGFX_Sprite& sprite, const char* path) {
#if !SMART_WEBRADIO_320_USE_VLW_FONTS
  (void)path;
  sprite.unloadFont();
  sprite.setTextSize(1);
  sprite.setTextWrap(false);
  sprite.setTextDatum(TL_DATUM);
  return false;
#else
  sprite.unloadFont();
  sprite.setTextSize(1);
  sprite.setTextWrap(false);
  sprite.setTextDatum(TL_DATUM);

#if SMART_WEBRADIO_320_CACHE_VLW_FONTS
  Ui320FontCache* cache = findFontCache(path);
  if (loadFontData(cache)) {
    if (sprite.loadFont((const uint8_t*)cache->data)) return true;
    if (!cache->drawFailReported) {
      Serial.print("[UI170 draw RAM font load FAILED, fallback SPIFFS] ");
      Serial.println(path);
    }
    sprite.unloadFont();
  }
#else
  Ui320FontCache* cache = findFontCache(path);
#endif

  if (!sprite.loadFont(SPIFFS, path)) {
    String fb = fallbackRootPath(path);
    if (fb != path && sprite.loadFont(SPIFFS, fb.c_str())) return true;
    if (!cache || !cache->drawFailReported) {
      Serial.print("[UI170 draw font load FAILED] ");
      Serial.println(path);
    }
    if (cache) cache->drawFailReported = true;
    sprite.unloadFont();
    return false;
  }
  return true;
#endif
}

bool ui320UseFont(LGFX_Sprite& sprite, Ui320FontRole role) {
  const char* path = fontPath(role);
  static uint32_t reportedMask = 0;
  uint32_t bit = (role >= 0 && role < 32) ? (1UL << role) : 0;
  bool ok = ui320UseFontPath(sprite, path);
  if (bit && !(reportedMask & bit)) {
    Serial.print(ok ? "[UI170 draw font loaded] " : "[UI170 draw font FAILED] ");
    Serial.println(path);
    reportedMask |= bit;
  }
  return ok;
}

int ui320TextWidth(LGFX_Sprite& sprite, const String& text, Ui320FontRole role) {
  bool vlwOk = ui320UseFont(sprite, role);
  if (!vlwOk) {
    sprite.setFont(role == UI320_FONT_STATION ? &fonts::Font4 : &fonts::Font2);
  }
  int w = sprite.textWidth(text);
  sprite.unloadFont();
  return w;
}

// ── Belső rajzoló segédfüggvények ─────────────────────────────────────────────

static void drawText(LGFX_Sprite& sprite, Ui320FontRole role, int x, int y, const String& text, uint16_t color) {
  bool vlwOk = ui320UseFont(sprite, role);
  sprite.setTextColor(color);
  sprite.setTextDatum(TL_DATUM);
  if (vlwOk) {
    sprite.drawString(text, x, y);
    sprite.unloadFont();
  } else {
    sprite.setFont(role == UI320_FONT_STATION ? &fonts::Font4 : &fonts::Font2);
    sprite.drawString(text, x, y + 2);
    sprite.unloadFont();
  }
}

static String fitText(LGFX_Sprite& sprite, const String& text, Ui320FontRole role, int maxW) {
  if (ui320TextWidth(sprite, text, role) <= maxW) return text;
  String out = text;
  const String ellipsis = "...";
  while (out.length() > 0 && ui320TextWidth(sprite, out + ellipsis, role) > maxW)
    out.remove(out.length() - 1);
  return out.length() ? out + ellipsis : ellipsis;
}

static void drawCenteredText(LGFX_Sprite& sprite, Ui320FontRole role, int x, int y, int w, const String& text, uint16_t color) {
  String shown = fitText(sprite, text, role, w);
  int textW = ui320TextWidth(sprite, shown, role);
  drawText(sprite, role, x + max(0, (w - textW) / 2), y, shown, color);
}

static int fontBoxHeight(Ui320FontRole role) {
  switch (role) {
    case UI320_FONT_STATION:
    case UI320_FONT_ARTIST:
    case UI320_FONT_TITLE:     return 24;
    case UI320_FONT_INFO:
    case UI320_FONT_INFO_BOLD: return 20;
    case UI320_FONT_BOLD:      return 20;
    case UI320_FONT_NORMAL:    return 17;
    case UI320_FONT_SMALL:     return 15;
    default:                   return 18;
  }
}

static void drawCenteredInBox(LGFX_Sprite& sprite, Ui320FontRole role, int x, int y, int w, int h, const String& text, uint16_t color) {
  int textY = y + max(0, (h - fontBoxHeight(role)) / 2);
  drawCenteredText(sprite, role, x, textY, w, text, color);
}

static void drawClippedTextLine(LGFX_Sprite& sprite, Ui320FontRole role, int line,
                                int x, int y, int w, int h,
                                const String& text, int scrollLine, int scrollOffset, uint16_t color) {
  int textW = ui320TextWidth(sprite, text, role);
  int tx = 0;
  if (textW <= w) {
    tx = max(0, (w - textW) / 2);
  } else if (line == scrollLine) {
    tx = -scrollOffset;
  }
  sprite.setClipRect(x, y, w, h);
  int textY = y + max(0, (h - fontBoxHeight(role)) / 2);
  drawText(sprite, role, x + tx, textY, text, color);
  sprite.clearClipRect();
}

static int chipWidth(LGFX_Sprite& sprite, const String& text) {
  return ui320TextWidth(sprite, text, UI320_FONT_BOLD) + 16;
}

static void drawChip(LGFX_Sprite& sprite, int x, int y, int w, const String& text, uint16_t color, bool filled) {
  const int chipH = 22;
  uint16_t textColor = filled ? TFT_BLACK : color;
  if (filled) sprite.fillRoundRect(x, y, w, chipH, 4, color);
  else        sprite.drawRoundRect(x, y, w, chipH, 4, color);
  String shown = fitText(sprite, text, UI320_FONT_BOLD, w - 12);
  int textY = y + max(0, (chipH - fontBoxHeight(UI320_FONT_BOLD)) / 2);
  drawText(sprite, UI320_FONT_BOLD, x + 8, textY, shown, textColor);
}

// ── Boot képernyő ─────────────────────────────────────────────────────────────
//
//  320 x 170 px
//  ┌──────────────────────────────────────┐  y=0
//  │  Smart WebRadio                      │  y=4  (kártya y=4, h=162)
//  │  line1                               │  y=50
//  │  line2                               │  y=76
//  │  line3                               │  y=102
//  └──────────────────────────────────────┘  y=166

void ui320DrawBoot(LGFX_Sprite& sprite, lgfx::LGFX_Device& tft,
                   const String& line1, const String& line2, const String& line3) {
  uint16_t bg     = TFT_BLACK;
  uint16_t accent = TFT_GREEN;
  uint16_t text   = TFT_WHITE;
  uint16_t muted  = TFT_CYAN;
  uint16_t cardBg = tft.color565(32, 73, 93);

  sprite.setTextWrap(false);
  sprite.fillScreen(bg);
  sprite.fillRoundRect(4, 4, 312, 162, 6, cardBg);
  sprite.drawRoundRect(4, 4, 312, 162, 6, TFT_WHITE);
  drawText(sprite, UI320_FONT_INFO_BOLD, 16, 14, "Smart WebRadio", accent);
  drawText(sprite, UI320_FONT_BOLD,      16, 50,  line1, text);
  if (line2.length()) drawText(sprite, UI320_FONT_BOLD, 16, 76,  line2, muted);
  if (line3.length()) drawText(sprite, UI320_FONT_BOLD, 16, 102, line3, muted);
}

// ── Főképernyő ────────────────────────────────────────────────────────────────
//
//  320 x 170 px  (landscape)
//
//  y=  0 ┌──────────────────────────────────────────────┐
//        │  Volume  ████████░░░░░░░░░░  [●]             │  h=26  hangerő sáv
//  y= 28 ├──────────────────────────────────────────────┤
//        │  Állomás neve                                 │
//        │  Előadó                                       │  h=108 kártya
//        │  Dal cím                                      │
//        │  [Ország]          [kbps]          [Műfaj]   │
//  y=138 ├──────────────────────────────────────────────┤
//        │  tipp szöveg (hint)                           │  h=28
//  y=168 └──────────────────────────────────────────────┘

void ui320DrawMain(LGFX_Sprite& sprite, lgfx::LGFX_Device& tft, const Ui320ScreenState& state) {
  const uint16_t bg      = TFT_BLACK;
  const uint16_t border  = TFT_WHITE;
  const uint16_t accent  = TFT_GREEN;
  const uint16_t cyan    = TFT_CYAN;
  const uint16_t warn    = TFT_YELLOW;
  const uint16_t topBg   = tft.color565(50, 0, 60);
  const uint16_t cardBg  = tft.color565(32, 73, 93);
  const uint16_t dim     = 0x2104;

  sprite.setTextWrap(false);
  sprite.fillScreen(bg);

  const int margin   = 4;
  const int contentW = 312;   // 320 - 2*margin

  // ── Hangerő sáv  y=2, h=26 ───────────────────────────────────────────────
  sprite.fillRoundRect(margin, 2, contentW, 26, 4, topBg);
  sprite.drawRoundRect(margin, 2, contentW, 26, 4, border);
  drawText(sprite, UI320_FONT_INFO_BOLD, 14, 6, "Volume", accent);

  int volumeMax  = max(1, state.volumeMax);
  int activeBars = constrain((state.volume * 18) / volumeMax, 0, 18);
  for (int i = 0; i < 18; i++) {
    uint16_t c = (i < activeBars) ? accent : dim;
    sprite.fillRoundRect(96 + i * 8, 10, 6, 7, 2, c);
  }

  // Hosszú nyomás körjelző – jobb szélen
  const int holdX = 302;
  const int holdY = 15;
  const int holdR = 7;
  sprite.drawCircle(holdX, holdY, holdR, border);
  if (state.buttonHolding) {
    int prog = constrain((int)(state.holdElapsedMs / 100), 1, holdR);
    for (int r = 1; r <= prog; r++) sprite.drawCircle(holdX, holdY, r, accent);
  }

  // ── Állomás kártya  y=30, h=108 ──────────────────────────────────────────
  bool     stationFocused = (state.mode != 0 && state.focusIndex == 0);
  uint16_t cardBorder     = stationFocused ? warn : border;
  const int cardX = margin;
  const int cardY = 30;
  const int cardW = contentW;
  const int cardH = 108;
  sprite.fillRoundRect(cardX, cardY, cardW, cardH, 5, cardBg);
  sprite.drawRoundRect(cardX, cardY, cardW, cardH, 5, cardBorder);

  bool stationEdit = (state.mode == 2 && state.focusIndex == 0);
  if (stationEdit) {
    // Szerkesztő nézet – állomásválasztó
    drawCenteredText(sprite, UI320_FONT_INFO_BOLD, cardX + 8, cardY + 6, cardW - 16, "Station", warn);
    sprite.fillRoundRect(cardX + 16, cardY + 30, cardW - 32, 26, 4, warn);
    drawText(sprite, UI320_FONT_INFO_BOLD, cardX + 24,          cardY + 34, "<", TFT_BLACK);
    drawText(sprite, UI320_FONT_INFO_BOLD, cardX + cardW - 40,  cardY + 34, ">", TFT_BLACK);
    drawClippedTextLine(sprite, UI320_FONT_INFO_BOLD, -10,
                        cardX + 48, cardY + 31, cardW - 96, 24,
                        state.browseName, state.scrollLine, state.scrollOffset, TFT_BLACK);
    drawCenteredText(sprite, UI320_FONT_BOLD, cardX + 8, cardY + 55, cardW - 16, state.browseCount, warn);
  } else {
    // Normál / böngésző nézet – 3 szövegsáv, egyenletesen elosztva
    // Kártya belső területe: y+6 … y+cardH-6  → ~96px, 3 × 32px-es sáv
    drawClippedTextLine(sprite, UI320_FONT_STATION, 0,
                        cardX + 10, cardY + 6,  cardW - 20, 26,
                        state.station, state.scrollLine, state.scrollOffset,
                        stationFocused ? warn : TFT_WHITE);
    // Előadó függőleges korrekciója: -8 px, dalcím további -7 px
    const int metaYOffset = -8;
    const int artistY = cardY + 38 + metaYOffset;
    const int titleY  = cardY + 70 + metaYOffset - 7;

    drawClippedTextLine(sprite, UI320_FONT_ARTIST, 1,
                        cardX + 10, artistY, cardW - 20, 26,
                        state.artist, state.scrollLine, state.scrollOffset, cyan);
    drawClippedTextLine(sprite, UI320_FONT_TITLE, 2,
                        cardX + 10, titleY, cardW - 20, 26,
                        state.title, state.scrollLine, state.scrollOffset, TFT_WHITE);
  }

  // ── Chip sor (ország / bitráta / műfaj)  a kártya alján ──────────────────
  int countryW  = chipWidth(sprite, state.country);
  int genreW    = chipWidth(sprite, state.genre);
  int chipsY    = cardY + cardH - 26;
  int chipLeft  = cardX + 8;
  int chipRight = cardX + cardW - 8;
  int genreX    = chipRight - genreW;

  drawChip(sprite, chipLeft, chipsY, countryW, state.country,
           (state.focusIndex == 1) ? warn : accent,
           state.mode == 2 && state.focusIndex == 1);
  drawChip(sprite, genreX,   chipsY, genreW,   state.genre,
           (state.focusIndex == 2) ? warn : cyan,
           state.mode == 2 && state.focusIndex == 2);

  // Audio státusz chip középen, csak ha elfér
  int    leftEdge   = chipLeft + countryW;
  int    rightEdge  = genreX;
  int    midCenter  = (leftEdge + rightEdge) / 2;
  String audioText  = state.audioStatus;
  int    audioW     = chipWidth(sprite, audioText);
  int    audioX     = midCenter - audioW / 2;
  if (!(leftEdge + 6 <= audioX && audioX + audioW + 6 <= rightEdge)) {
    int sep = audioText.indexOf(' ');
    if (sep > 0) {
      audioText = audioText.substring(0, sep);
      audioW    = chipWidth(sprite, audioText);
      audioX    = midCenter - audioW / 2;
    }
  }
  if (leftEdge + 6 <= audioX && audioX + audioW + 6 <= rightEdge) {
    drawChip(sprite, audioX, chipsY, audioW, audioText, border, false);
  }

  // ── Tipp sáv  y=140, h=26 ────────────────────────────────────────────────
  sprite.fillRoundRect(margin, 140, contentW, 26, 4, bg);
  sprite.drawRoundRect(margin, 140, contentW, 26, 4, (state.mode == 0) ? border : accent);
  drawCenteredInBox(sprite, UI320_FONT_INFO_BOLD, margin + 4, 140, contentW - 8, 26, state.hint, TFT_WHITE);

  // ── PAUSE felirat (néma mód) ──────────────────────────────────────────────
  if (state.muted && state.mode == 0) {
    const int pauseX = 116;
    const int pauseY = cardY + 55;  // dalcím sor magassága
    const int pauseW = 88;
    const int pauseH = 24;
    sprite.fillRoundRect(pauseX, pauseY, pauseW, pauseH, 5, tft.color565(35, 35, 45));
    sprite.drawRoundRect(pauseX, pauseY, pauseW, pauseH, 5, warn);
    drawCenteredInBox(sprite, UI320_FONT_INFO_BOLD, pauseX, pauseY, pauseW, pauseH, "PAUSE", warn);
  }
}
