#include "OperatingModes.h"
#include "ui_drawing.hpp"

void printPowerSource(FontStyle style) {
  uint8_t srcNum = getPowerSourceNumber();

#ifdef POW_DC
  if (srcNum == 0 && getSettingValue(SettingsOptions::MinDCVoltageCells)) {
    // DC input powered from a configured lithium battery pack (3S-6S):
    // show a battery gauge with the cell count (e.g. "4S") instead of the generic "DC".
    OLED::drawBatterySmall(getDCPackLevel());
    OLED::print(SmallSymbolSpace, style);
    OLED::printNumber(2 + getSettingValue(SettingsOptions::MinDCVoltageCells), 1, style);
    OLED::print(SmallSymbolCellCount, style);
    return;
  }
#endif

  OLED::drawSymbolSmall(0);
  OLED::print(SmallSymbolSpace, style);

  if (srcNum < 2) {
    OLED::print(PowerSourceNames[srcNum], style);
  } else {
    OLED::print(SmallSymbolPD, style);
  }
}