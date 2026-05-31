#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <LovyanGFX.hpp>

// 160x128 UI font layer.
// The current SPIFFS layout stores VLW files under /fonts, e.g. /fonts/test_10.vlw.
// This file intentionally contains only font selection helpers; layout and radio logic stay in the .ino.

enum Ui160FontRole {
  UI160_FONT_SMALL,
  UI160_FONT_NORMAL,
  UI160_FONT_BOLD,
  UI160_FONT_INFO,
  UI160_FONT_INFO_BOLD,
  UI160_FONT_ARTIST,
  UI160_FONT_TITLE,
  UI160_FONT_STATION
};

bool ui160BeginFonts();
uint32_t ui160FontFileSize(const char* path);
bool ui160UseFontPath(LGFX_Sprite& sprite, const char* path);
bool ui160UseFont(LGFX_Sprite& sprite, Ui160FontRole role);
int ui160TextWidth(LGFX_Sprite& sprite, const String& text, Ui160FontRole role);
int ui160StatusTextWidth(LGFX_Sprite& sprite, const String& text);