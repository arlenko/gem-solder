#include "ui_drawing.hpp"
#ifdef OLED_128x32

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
    const uint8_t statusW  = 5 * 6;                // "NN.NW" / "NN.NV" 5 cells
    const uint8_t tempW    = (3 * 12) + 10;        // 3 large digits + small "°C"
    const uint8_t tempZone = UI_DRAWING_TEMP_ZONE; // Left of the status block
    const int16_t tempX    = tempZone - tempW;
    const int16_t statusX  = OLED_MAX_X - statusW;

    if (!(getSettingValue(SettingsOptions::CoolingTempBlink) && (tipTemp > 55) && (xTaskGetTickCount() % 1000 < 300))) {
      // Blink temp if setting enable and temp < 55° (OFF 300ms / ON 700ms)
      OLED::setCursor(tempX, 4); // 24px number, vertically centred (4px above/below)
      ui_draw_tip_temperature(true, FontStyle::LARGE);
    }

    // Set temperature
    OLED::setCursor(1, 4);
    OLED::printNumber(getSettingValue(SettingsOptions::SolderingTemp), 3, FontStyle::LITTLE);
    OLED::printSymbolDeg(FontStyle::LITTLE);

    // Input voltage
    OLED::setCursor(OLED_MAX_X - 30, 16);
    printVoltage(FontStyle::LITTLE);
    OLED::print(SmallSymbolVolts, FontStyle::LITTLE);

    // Power source
    OLED::setCursor(OLED_MAX_X - 30, 4);
    printPowerSource(FontStyle::LITTLE);

    // One slot remains empty. Can use it later once we implement a tip type support
  }
}
#endif