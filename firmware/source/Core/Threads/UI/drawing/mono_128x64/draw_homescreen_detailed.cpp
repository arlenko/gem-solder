#include "ui_drawing.hpp"
#ifdef OLED_128x64

extern uint8_t buttonAF[sizeof(buttonA)];
extern uint8_t buttonBF[sizeof(buttonB)];
extern uint8_t disconnectedTipF[sizeof(disconnectedTip)];

void ui_draw_homescreen_detailed(TemperatureType_t tipTemp) {
  if (isTipDisconnected()) {
    if (OLED::getRotation()) {
      // in right handed mode we want to draw over the first part
      OLED::drawArea(56, 0, 56, 32, disconnectedTipF);
    } else {
      OLED::drawArea(0, 0, 56, 32, disconnectedTip);
    }
    if (OLED::getRotation()) {
      OLED::setCursor(10, 8);
    } else {
      OLED::setCursor(62, 8);
    }
    uint32_t Vlt = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
    OLED::printNumber(Vlt / 10, 2, FontStyle::SMALL);
    OLED::print(SmallSymbolDot, FontStyle::SMALL);
    OLED::printNumber(Vlt % 10, 1, FontStyle::SMALL);
    OLED::print(SmallSymbolVolts, FontStyle::SMALL);
    OLED::setCursor(116, 0);
    ui_draw_power_source_icon();
  } else {
    const uint8_t statusW = 5 * 8;              // "NN.NW" / "NN.NV" are 5 cells in the 8x16 small font
    const uint8_t tempW   = (3 * 16) + (2 * 8); // 3 large digits + small "°C"
    const int16_t tempX   = (OLED_MAX_X - tempW) / 2 + 4;

    // Set temperature
    OLED::setCursor(1, 0);
    OLED::printNumber(getSettingValue(SettingsOptions::SolderingTemp), 3, FontStyle::SMALL);
    OLED::printSymbolDeg(FontStyle::SMALL);

    // Handle type
    OLED::setCursor(OLED_MAX_X - 32, 0);
    printHandleType();

    // Draw heat symbol
    ui_draw_heat_status(1, 21, false);

    if (!(getSettingValue(SettingsOptions::CoolingTempBlink) && (tipTemp > 55) && (xTaskGetTickCount() % 1000 < 300))) {
      // Blink temp if setting enable and temp < 55° (OFF 300ms / ON 700ms)
      OLED::setCursor(tempX, 16);
      ui_draw_tip_temperature(false, FontStyle::XLARGE);
      OLED::setCursor(OLED::getCursorX(), OLED::getCursorY() + 3);
      OLED::printSymbolDeg(FontStyle::SMALL);
    }

    // Power source icon
    OLED::setCursor(115, 16);
    ui_draw_power_source_icon();

    // Power source
    OLED::setCursor(OLED_MAX_X - statusW - 24, 49);
    printPowerSource();

    // Input voltage
    OLED::setCursor(OLED_MAX_X - statusW, 49);
    printVoltage();
    OLED::print(SmallSymbolVolts, FontStyle::SMALL);
  }
}
#endif