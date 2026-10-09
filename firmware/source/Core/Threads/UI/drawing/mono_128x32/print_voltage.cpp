#include "ui_drawing.hpp"
#ifdef OLED_128x32

void printVoltage(FontStyle style) {
  uint32_t volt = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0);
  OLED::printNumber(volt / 10, 2, style);
  OLED::print(SmallSymbolDot, style);
  OLED::printNumber(volt % 10, 1, style);
}
#endif