#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <LovyanGFX.hpp>

// 320x170 UI layer – LILYGO T-Display S3 beépített kijelzőhöz.
// Önálló fájl, nem módosítja az ui_320x240.* forrásokat.
// VLW fontok cache-elve, hogy az újrarajzolás ne blokkolja az audiót.

#ifndef SMART_WEBRADIO_320_USE_VLW_FONTS
#define SMART_WEBRADIO_320_USE_VLW_FONTS 1
#endif

#ifndef SMART_WEBRADIO_320_CACHE_VLW_FONTS
#define SMART_WEBRADIO_320_CACHE_VLW_FONTS 1
#endif

enum Ui320FontRole {
  UI320_FONT_SMALL,
  UI320_FONT_NORMAL,
  UI320_FONT_BOLD,
  UI320_FONT_INFO,
  UI320_FONT_INFO_BOLD,
  UI320_FONT_ARTIST,
  UI320_FONT_TITLE,
  UI320_FONT_STATION
};

struct Ui320ScreenState {
  String station;
  String artist;
  String title;
  String country;
  String genre;
  String audioStatus;
  String hint;
  String browseName;
  String browseCount;
  int volume;
  int volumeMax;
  int mode;
  int focusIndex;
  int scrollLine;
  int scrollOffset;
  bool muted;
  bool buttonHolding;
  uint32_t holdElapsedMs;
};

bool ui320BeginFonts();
uint32_t ui320FontFileSize(const char* path);
bool ui320UseFontPath(LGFX_Sprite& sprite, const char* path);
bool ui320UseFont(LGFX_Sprite& sprite, Ui320FontRole role);
int ui320TextWidth(LGFX_Sprite& sprite, const String& text, Ui320FontRole role);
void ui320DrawBoot(LGFX_Sprite& sprite, lgfx::LGFX_Device& tft, const String& line1, const String& line2, const String& line3);
void ui320DrawMain(LGFX_Sprite& sprite, lgfx::LGFX_Device& tft, const Ui320ScreenState& state);
