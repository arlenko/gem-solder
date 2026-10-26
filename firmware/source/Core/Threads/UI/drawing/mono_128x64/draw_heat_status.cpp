#include "power.hpp"
#include "ui_drawing.hpp"
#include <OperatingModes.h>

#ifdef OLED_128x64
void ui_draw_heat_status(int16_t posX, bool boostModeOn) {
  // Heat symbol
  OLED::setCursor(posX, 0);
  OLED::drawHeatSymbol(x10WattHistory.average());
  // Boost mode indicator next to heat symbol
  if (boostModeOn) {
    OLED::drawSymbol(2);
  } else {
    OLED::print(LargeSymbolSpace, FontStyle::LARGE);
  }
}
#endif