#include "power.hpp"
#include "ui_drawing.hpp"
#include <OperatingModes.h>

void ui_draw_heat_status(int16_t posX, bool boostModeOn) {
  // Heat symbol
  OLED::setCursor(posX, 0);
  OLED::drawHeatSymbol(X10WattsToPWM(x10WattHistory.average()));
  // Boost mode indicator next to heat symbol
  if (boostModeOn) {
    OLED::drawSymbol(2);
  } else {
    OLED::print(LargeSymbolSpace, FontStyle::LARGE);
  }
}