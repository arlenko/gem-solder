#include "power.hpp"
#include "ui_drawing.hpp"
#include <OperatingModes.h>

void ui_draw_heat_status(bool boostModeOn) {
  const bool    rot         = OLED::getRotation();
  const uint8_t heatSymbolW = 12;
  const int16_t heatSymbolX = rot ? (OLED_WIDTH - 1 - heatSymbolW) : 0;
  const int16_t boostX      = rot ? (heatSymbolX - heatSymbolW) : heatSymbolW;
  const uint8_t posY        = 0;

  // Heat symbol. Top row opposite side to status
  OLED::setCursor(heatSymbolX, posY);
  OLED::drawHeatSymbol(X10WattsToPWM(x10WattHistory.average()));
  // Boost mode indicator next to heat symbol
  OLED::setCursor(boostX, posY);
  if (boostModeOn) {
    OLED::drawSymbol(2);
  } else {
    OLED::print(LargeSymbolSpace, FontStyle::LARGE);
  }
}