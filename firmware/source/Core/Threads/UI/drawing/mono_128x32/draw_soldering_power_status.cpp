#include "power.hpp"
#include "ui_drawing.hpp"
#include <OperatingModes.h>
#ifdef OLED_128x32

void printWattage(uint32_t x10Watt, FontStyle style) {
  if (x10Watt > 999) {
    // Above 99.9W drop the decimal place to keep it to 5 cells
    OLED::print(SmallSymbolSpace, style);
    OLED::printNumber(x10Watt / 10, 3, style);
  } else {
    OLED::printNumber(x10Watt / 10, 2, style);
    OLED::print(SmallSymbolDot, style);
    OLED::printNumber(x10Watt % 10, 1, style);
  }
  OLED::print(SmallSymbolWatts, style);
}

#ifdef PID_DEBUG
extern volatile int32_t PIDDebugResults[3];

void printPIDTerm(int32_t x10Watt, FontStyle style) {
  if (x10Watt < 0) {
    // Print minus for negative numbers and invert to positive
    OLED::print(SmallSymbolMinus, style);
    x10Watt = -x10Watt;
  } else {
    OLED::print(SmallSymbolSpace, style);
  }

  if (x10Watt > 9999) {
    x10Watt = 9999; // Clamp
  }

  printWattage(x10Watt, style);
}
#endif

void ui_draw_soldering_power_status(bool boost_mode_on) {
#ifdef PID_DEBUG
  OLED::setCursor(8, 0);
  ui_draw_tip_temperature(true, FontStyle::SMALL);
  // OLED::setCursor(0, 0);
  // uint32_t x10Watt = x10WattHistory.average();
  // printWattage(x10Watt, FontStyle::SMALL);
  OLED::setCursor(80, 0);
  printPIDTerm(PIDDebugResults[0], FontStyle::SMALL);
  OLED::setCursor(0, 16);
  printPIDTerm(PIDDebugResults[1], FontStyle::SMALL);
  OLED::setCursor(80, 16);
  printPIDTerm(PIDDebugResults[2], FontStyle::SMALL);
#else
  // One-line LARGE (12x24) tip temperature flush to one edge, vertically
  // centred; two SMALL (8x16) status rows (wattage, voltage) flush to the
  // other edge. The power-source label is dropped to keep two larger rows.
  // Sides flip with rotation.
  (void)boost_mode_on;
  const uint8_t statusW  = 5 * 8;                // "NN.NW" / "NN.NV" are 5 cells in the 8x16 small font
  const uint8_t tempW    = (3 * 12) + (2 * 8);   // 3 large digits + small "°C" = 52px
  const uint8_t tempZone = UI_DRAWING_TEMP_ZONE; // Left of the status block
  const int16_t tempX    = tempZone - tempW;
  const int16_t statusX  = OLED_MAX_X - statusW;

  // Draw heat symbol
  ui_draw_heat_status(0, boost_mode_on);

  // 24px number, vertically centred (4px above/below)
  OLED::setCursor(tempX, 4);
  ui_draw_tip_temperature(false, FontStyle::LARGE);
  // Degree + unit in the small font, bottom-aligned with the 24px number (like the TS100)
  OLED::setCursor(OLED::getCursorX(), 12);
  OLED::printSymbolDeg(FontStyle::SMALL);

  // Wattage (top row)
  OLED::setCursor(statusX, 0);
  uint32_t x10Watt = x10WattHistory.average();
  printWattage(x10Watt, FontStyle::SMALL);

  // Input voltage (bottom row)
  OLED::setCursor(statusX, 16);
  printVoltage();
  OLED::print(SmallSymbolVolts, FontStyle::SMALL);
#endif
}
#endif
