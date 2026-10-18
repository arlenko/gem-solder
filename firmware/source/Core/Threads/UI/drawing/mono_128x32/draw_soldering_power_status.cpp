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
  // PID terms (first row)
  OLED::setCursor(1, 0);
  printPIDTerm(PIDDebugResults[0], FontStyle::LITTLE);
  OLED::setCursor(46, 0);
  printPIDTerm(PIDDebugResults[1], FontStyle::LITTLE);
  OLED::setCursor(OLED_MAX_X - 36, 0);
  printPIDTerm(PIDDebugResults[2], FontStyle::LITTLE);
  // Wattage and temperaute (second row)
  OLED::setCursor(7, 12);
  uint32_t x10Watt = x10WattHistory.average();
  printWattage(x10Watt, FontStyle::LITTLE);
  OLED::setCursor(52, 12);
  ui_draw_tip_temperature(true, FontStyle::LITTLE);
#else
  (void)boost_mode_on;
  const uint8_t statusW  = 5 * 6;                // "NN.NW" / "NN.NV" 5 cells
  const uint8_t tempW    = (3 * 12) + 10;        // 3 large digits + small "°C"
  const uint8_t tempZone = UI_DRAWING_TEMP_ZONE; // Left of the status block
  const int16_t tempX    = tempZone - tempW;
  const int16_t statusX  = OLED_MAX_X - statusW;

  // Temperature
  OLED::setCursor(tempX, 0);
  ui_draw_tip_temperature(true, FontStyle::LARGE);

  // Set temperature
  OLED::setCursor(1, 0);
  if (boost_mode_on) {
    if (xTaskGetTickCount() % 1000 > 300) {
      // Blink temp if boost is active (OFF 300ms / ON 700ms)
      OLED::printNumber(getSettingValue(SettingsOptions::BoostTemp), 3, FontStyle::LITTLE);
      OLED::printSymbolDeg(FontStyle::LITTLE);
    }
  } else {
    OLED::printNumber(getSettingValue(SettingsOptions::SolderingTemp), 3, FontStyle::LITTLE);
    OLED::printSymbolDeg(FontStyle::LITTLE);
  }

  // Wattage
  OLED::setCursor(1, 12);
  uint32_t x10Watt = x10WattHistory.average();
  printWattage(x10Watt, FontStyle::LITTLE);

  // Input voltage
  OLED::setCursor(OLED_MAX_X - 30, 12);
  printVoltage(FontStyle::LITTLE);
  OLED::print(SmallSymbolVolts, FontStyle::LITTLE);

  // Power source
  OLED::setCursor(OLED_MAX_X - 30, 0);
  printPowerSource(FontStyle::LITTLE);
#endif

  // Power bar
  ui_draw_power_bar(1, OLED_MAX_Y - POWER_BAR_HEIGHT, POWER_BAR_WIDTH, POWER_BAR_HEIGHT, PowerBarAnimation::Unfold);
}
#endif
