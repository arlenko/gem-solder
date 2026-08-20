#include "power.hpp"
#include "ui_drawing.hpp"
#ifdef OLED_128x32

void ui_draw_soldering_basic_status(bool boostModeOn) {
  OLED::setCursor(0, 0);
  ui_draw_heat_status(0, boostModeOn);
  // Draw current tip temp (y=4 centres the 24px number; +12 nudges it one digit right; restore after)
  OLED::setCursor(OLED::getCursorX() + 12, 4);
  ui_draw_tip_temperature(true, FontStyle::LARGE);
  OLED::setCursor(OLED::getCursorX(), 0);
  // Power source icon near the right edge (matches the simplified idle screen)
  OLED::setCursor(116, 0);
  ui_draw_power_source_icon();
}

#endif