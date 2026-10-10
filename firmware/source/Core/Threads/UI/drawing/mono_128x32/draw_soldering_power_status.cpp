#include "power.hpp"
#include "ui_drawing.hpp"
#include <OperatingModes.h>
#ifdef OLED_128x32

#ifdef PID_DEBUG
extern volatile int32_t PIDDebugResults[3];
void                    print_debug_result(int32_t value) {
  if (value < 0) {
    OLED::print(SmallSymbolMinus, FontStyle::SMALL);
    value = -value;
  } else {
    OLED::print(SmallSymbolSpace, FontStyle::SMALL);
  }
  OLED::printNumber(value, 5, FontStyle::SMALL);
}

void ui_draw_soldering_power_status(bool boost_mode_on) {
  OLED::setCursor(16, 0);
  ui_draw_tip_temperature(false, FontStyle::SMALL);
  OLED::printSymbolDeg(FontStyle::SMALL);
  OLED::setCursor(64, 0);
  print_debug_result(PIDDebugResults[0]);
  OLED::setCursor(0, 16);
  print_debug_result(PIDDebugResults[1]);
  OLED::setCursor(64, 16);
  print_debug_result(PIDDebugResults[2]);
}
#else

void ui_draw_soldering_power_status(bool boost_mode_on) {
  (void)boost_mode_on;
  const uint8_t statusW  = 5 * 6;                // "NN.NW" / "NN.NV" 5 cells
  const uint8_t tempW    = (3 * 12) + 10;        // 3 large digits + small "°C"
  const uint8_t tempZone = UI_DRAWING_TEMP_ZONE; // Left of the status block
  const int16_t tempX    = tempZone - tempW;
  const int16_t statusX  = OLED_MAX_X - statusW;
  const uint8_t powerBarWidth  = OLED_WIDTH - 2; // 1px gaps on both sides
  const uint8_t powerBarHeight = 6;

  // Temperature
  OLED::setCursor(tempX, 0);
  ui_draw_tip_temperature(true, FontStyle::LARGE);

  // Set temperature
  OLED::setCursor(1, 0);
  OLED::printNumber(getSettingValue(SettingsOptions::SolderingTemp), 3, FontStyle::LITTLE);
  OLED::printSymbolDeg(FontStyle::LITTLE);

  // Wattage
  OLED::setCursor(1, 12);
  {
    uint32_t x10Watt = x10WattHistory.average();
    if (x10Watt > 999) {
      // Above 99.9W drop the decimal place to keep it to 5 cells
      OLED::print(SmallSymbolSpace, FontStyle::LITTLE);
      OLED::printNumber(x10Watt / 10, 3, FontStyle::LITTLE);
    } else {
      OLED::printNumber(x10Watt / 10, 2, FontStyle::LITTLE);
      OLED::print(SmallSymbolDot, FontStyle::LITTLE);
      OLED::printNumber(x10Watt % 10, 1, FontStyle::LITTLE);
    }
    OLED::print(SmallSymbolWatts, FontStyle::LITTLE);
  }

  // Input voltage
  OLED::setCursor(OLED_MAX_X - 30, 12);
  printVoltage(FontStyle::LITTLE);
  OLED::print(SmallSymbolVolts, FontStyle::LITTLE);

  // Power source
  OLED::setCursor(OLED_MAX_X - 30, 0);
  printPowerSource(FontStyle::LITTLE);

  // Power bar
  ui_draw_power_bar(1, OLED_MAX_Y - powerBarHeight, powerBarWidth, powerBarHeight);
}
#endif
#endif
