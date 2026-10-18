#include "power.hpp"
#include "ui_drawing.hpp"

void ui_draw_power_bar(uint8_t posX, uint8_t posY, uint8_t width, uint8_t height, PowerBarAnimation animation) {
  const int32_t  limit         = getX10WattageLimits();
  const uint32_t power         = x10WattHistory.average();
  uint8_t        usableWidth   = width - 2; // 1px border each side
  static uint8_t currentHeight = 1;

  uint32_t scaled = 0;
  if (limit > 0) {
    scaled = (power * usableWidth) / (uint32_t)limit;
    scaled = scaled > usableWidth ? usableWidth : scaled; // Clamp
  }
  const uint8_t fillWidth = (uint8_t)scaled;

  switch (animation) {
  case PowerBarAnimation::Unfold:
    if (currentHeight < height)
      currentHeight += 1;
    break;
  case PowerBarAnimation::Fold:
    if (currentHeight > 1)
      currentHeight -= 1;
    break;
  default:
    currentHeight = height;
    break;
  }

  // Animate around the vertical centre of the bar
  const uint8_t barTop    = posY + ((height - currentHeight) / 2);
  const uint8_t barBottom = barTop + currentHeight - 1;

  // Draw the full rect (outline + fill), then erase the interior to the right of the fill level
  OLED::drawFilledRect(posX, barTop, posX + width - 1, barBottom, false);
  if (fillWidth < usableWidth && currentHeight > 2) {
    OLED::drawFilledRect(posX + 1 + fillWidth, barTop + 1, posX + width - 2, barBottom - 1, true);
  }
}