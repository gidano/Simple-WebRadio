#include "ui_160x128.h"
#include <stdlib.h>

static const char* fontPath(Ui160FontRole role) {
  switch (role) {
    case UI160_FONT_SMALL:     return "/fonts/test_9.vlw";
    case UI160_FONT_NORMAL:    return "/fonts/test_10.vlw";
    case UI160_FONT_BOLD:      return "/fonts/test_sb_10.vlw";
    case UI160_FONT_INFO:      return "/fonts/test_12.vlw";
    case UI160_FONT_INFO_BOLD: return "/fonts/test_sb_12.vlw";
    case UI160_FONT_ARTIST:    return "/fonts/test_sb_12.vlw";
    case UI160_FONT_TITLE:     return "/fonts/test_sb_12.vlw";
    case UI160_FONT_STATION:   return "/fonts/test_sb_14.vlw";
    default:                   return "/fonts/test_10.vlw";
  }
}

struct Ui160FontCache {
  const char* path;
  uint8_t* data;
  uint32_t size;
  bool tried;
  bool ok;
};

static Ui160FontCache fontCache[] = {
  { "/fonts/test_9.vlw", nullptr, 0, false, false },
  { "/fonts/test_10.vlw", nullptr, 0, false, false },
  { "/fonts/test_sb_10.vlw", nullptr, 0, false, false },
  { "/fonts/test_12.vlw", nullptr, 0, false, false },
  { "/fonts/test_sb_12.vlw", nullptr, 0, false, false },
  { "/fonts/test_sb_14.vlw", nullptr, 0, false, false }
};

static const int FONT_CACHE_COUNT = sizeof(fontCache) / sizeof(fontCache[0]);

static Ui160FontCache* findFontCache(const char* path) {
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

uint32_t ui160FontFileSize(const char* path) {
  Ui160FontCache* cache = findFontCache(path);
  if (cache && cache->ok) return cache->size;

  File f = SPIFFS.open(path, FILE_READ);
  if (!f) {
    String fallback = fallbackRootPath(path);
    if (fallback != path) f = SPIFFS.open(fallback, FILE_READ);
  }
  bool opened = (bool)f;
  uint32_t size = opened ? (uint32_t)f.size() : 0;
  if (opened) f.close();
  return size;
}

static bool loadFontData(Ui160FontCache* cache) {
  if (!cache) return false;
  if (cache->tried) return cache->ok;
  cache->tried = true;

  File f = SPIFFS.open(cache->path, FILE_READ);
  if (!f) {
    String fallback = fallbackRootPath(cache->path);
    if (fallback != cache->path) f = SPIFFS.open(fallback, FILE_READ);
  }
  if (!f) {
    Serial.print("[UI160 font cache open FAILED] ");
    Serial.println(cache->path);
    return false;
  }

  cache->size = (uint32_t)f.size();
  if (cache->size == 0) {
    f.close();
    Serial.print("[UI160 font cache empty] ");
    Serial.println(cache->path);
    return false;
  }

  cache->data = (uint8_t*)malloc(cache->size);
  if (!cache->data) {
    f.close();
    Serial.print("[UI160 font cache malloc FAILED] ");
    Serial.print(cache->path);
    Serial.print(" size=");
    Serial.println(cache->size);
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
    Serial.print("[UI160 font cache read FAILED] ");
    Serial.println(cache->path);
    return false;
  }

  cache->ok = true;
  Serial.print("[UI160 font cached] ");
  Serial.print(cache->path);
  Serial.print(" size=");
  Serial.print(cache->size);
  Serial.print(" heap=");
  Serial.println(ESP.getFreeHeap());
  return true;
}

static bool loadFontData(const char* path) {
  Ui160FontCache* cache = findFontCache(path);
  return loadFontData(cache);
}

bool ui160BeginFonts() {
  const Ui160FontRole required[] = {
    UI160_FONT_SMALL,
    UI160_FONT_NORMAL,
    UI160_FONT_BOLD,
    UI160_FONT_INFO,
    UI160_FONT_INFO_BOLD,
    UI160_FONT_ARTIST,
    UI160_FONT_TITLE,
    UI160_FONT_STATION
  };

  bool ok = true;
  for (Ui160FontRole role : required) {
    const char* path = fontPath(role);
    bool exists = SPIFFS.exists(path);
    uint32_t size = ui160FontFileSize(path);
    bool cached = loadFontData(path);

    Serial.print("[UI160 font check] ");
    Serial.print(path);
    Serial.print(" exists=");
    Serial.print(exists ? "YES" : "NO");
    Serial.print(" size=");
    Serial.print(size);
    Serial.print(" cached=");
    Serial.println(cached ? "YES" : "NO");

    if (!cached) ok = false;
  }
  return ok;
}

bool ui160UseFontPath(LGFX_Sprite& sprite, const char* path) {
  sprite.unloadFont();
  sprite.setTextSize(1);
  sprite.setTextWrap(false);
  sprite.setTextDatum(TL_DATUM);

  Ui160FontCache* cache = findFontCache(path);
  if (loadFontData(cache)) {
    if (sprite.loadFont((const uint8_t*)cache->data)) {
      return true;
    }
    Serial.print("[UI160 draw RAM font load FAILED, falling back to SPIFFS] ");
    Serial.println(path);
    sprite.unloadFont();
  }

  if (!sprite.loadFont(SPIFFS, path)) {
    String fallback = fallbackRootPath(path);
    if (fallback != path && sprite.loadFont(SPIFFS, fallback.c_str())) return true;
    Serial.print("[UI160 draw font load FAILED] ");
    Serial.println(path);
    return false;
  }
  return true;
}

bool ui160UseFont(LGFX_Sprite& sprite, Ui160FontRole role) {
  const char* path = fontPath(role);
  static uint32_t reportedMask = 0;
  uint32_t bit = (role >= 0 && role < 32) ? (1UL << role) : 0;

  bool ok = ui160UseFontPath(sprite, path);
  if (bit && !(reportedMask & bit)) {
    Serial.print(ok ? "[UI160 draw font loaded] " : "[UI160 draw font FAILED] ");
    Serial.println(path);
    reportedMask |= bit;
  }
  return ok;
}

int ui160TextWidth(LGFX_Sprite& sprite, const String& text, Ui160FontRole role) {
  ui160UseFont(sprite, role);
  int w = sprite.textWidth(text);
  sprite.unloadFont();
  return w;
}

int ui160StatusTextWidth(LGFX_Sprite& sprite, const String& text) {
  return ui160TextWidth(sprite, text, UI160_FONT_SMALL);
}
