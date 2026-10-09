#include "ui_drawing.hpp"
#include "OperatingModes.h"

void printPowerSource(FontStyle style) {
    uint8_t srcNum = getPowerSourceNumber();

    OLED::drawSymbolSmall(0);
    OLED::print(SmallSymbolSpace, style);
    if (srcNum < 2) {
        OLED::print(PowerSourceNames[srcNum], style);
    } else {
        OLED::print(SmallSymbolPD, style);
    }
}