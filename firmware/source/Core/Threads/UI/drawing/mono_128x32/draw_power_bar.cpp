#include "power.hpp"
#include "ui_drawing.hpp"

void ui_draw_power_bar(uint8_t posX, uint8_t posY, uint8_t width, uint8_t height) {
  const int32_t  limit       = getX10WattageLimits();
  const uint32_t power       = x10WattHistory.average();
  uint8_t        usableWidth = width - 2; // 1px border each side

  uint32_t scaled = 0;
  if (limit > 0) {
    scaled = (power * usableWidth) / (uint32_t)limit;
    scaled = scaled > usableWidth ? usableWidth : scaled; // Clamp
  }
  const uint8_t fillWidth = (uint8_t)scaled;

  // Draw the full rect (outline + fill), then erase the interior to the right of the fill level
  OLED::drawFilledRect(posX, posY, posX + width - 1, posY + height - 1, false);
  if (fillWidth < usableWidth) {
    OLED::drawFilledRect(posX + 1 + fillWidth, posY + 1, posX + width - 2, posY + height - 2, true);
  }
}