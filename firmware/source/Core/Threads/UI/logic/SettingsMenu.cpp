#include "OperatingModes.h"
#include "ScrollMessage.hpp"

#define HELP_TEXT_TIMEOUT_TICKS (TICKS_SECOND * 3)
#define MENU_ROW_HEIGHT         OLED_ROW_HEIGHT
#define MENU_ROWS               (OLED_HEIGHT / MENU_ROW_HEIGHT)
/*
 * The settings menu is the most complex bit of GUI code we have
 * The menu consists of a two tier menu
 * Main menu -> Categories
 * Secondary menu -> Settings
 *
 * For each entry in the menu
 */

/**
 * Prints two small lines (or one line for CJK) of short description for
 * setting items and prepares cursor after it.
 * @param settingsItemIndex Index of the setting item.
 * @param cursorCharPosition Custom cursor char position to set after printing
 * description.
 */
static void printShortDescription(SettingsItemIndex settingsItemIndex, uint16_t cursorCharPosition) {
  // print short description (default single line, explicit double line)
  uint8_t shortDescIndex = static_cast<uint8_t>(settingsItemIndex);
  OLED::printWholeScreen(translatedString(Tr->SettingsShortNames[shortDescIndex]));

  // prepare cursor for value (make room for the scroll indicator)
  // Keep the value edge-aligned on the 128px panel and centre the 24px font.
  OLED::setCursor((cursorCharPosition * FONT_12_WIDTH - 2) + (OLED_WIDTH - 96), 4);
}

uint16_t getMenuLength(const menuitem *menu, const uint16_t stop) {
  // walk this menu to find the length
  uint16_t counter = 0;
  for (uint16_t pos = 0; pos < stop; pos++) {
    // End of list
    if (menu[pos].draw == nullptr) {
      return counter;
    }
    // Otherwise increment for each visible item (null == always, or if not check function)
    if (menu[pos].isVisible == nullptr || menu[pos].isVisible()) {
      counter++;
    }
  }
  return counter;
}

/**
 * Translates a visible position in a menu (counting only visible entries) into
 * the raw index into the menu array. Returns 0xFFFF if not found.
 */
static uint16_t getVisibleEntryIndex(const menuitem *menu, const uint16_t visiblePosition) {
  uint16_t visible = 0;
  for (uint16_t pos = 0; menu[pos].draw != nullptr; pos++) {
    if (menu[pos].isVisible == nullptr || menu[pos].isVisible()) {
      if (visible == visiblePosition) {
        return pos;
      }
      visible++;
    }
  }
  return 0xFFFF;
}

/**
 * Virtual (visible) position of a raw menu entry.
 */
static uint16_t getActiveVirtualPosition(const menuitem *menu, const uint16_t rawIndex) {
  uint16_t position = getMenuLength(menu, rawIndex + 1);
  if (position > 0) {
    position--;
  }
  return position;
}

/**
 * First visible entry of the on-screen page. The active entry is kept at the top
 * of the screen unless that would leave the last page short, in which case the
 * page is clamped so the last entry sits at the bottom.
 */
static uint16_t getWindowStart(const menuitem *menu, const uint16_t rawIndex, const uint16_t menuLength) {
  if (menuLength == 0) {
    return 0;
  }
  const uint16_t visibleRows = (menuLength < MENU_ROWS) ? menuLength : MENU_ROWS;
  uint16_t       start       = getActiveVirtualPosition(menu, rawIndex);
  if (start + visibleRows > menuLength) {
    start = menuLength - visibleRows;
  }
  return start;
}

/**
 * Renders one menu entry into a single 32px row. Existing 32px-oriented item
 * drawing code is reused by moving the driver's drawing window to the row.
 */
static void renderMenuItem(const menuitem *item, const uint8_t row, const bool active) {
  OLED::setDrawingWindow(row * (MENU_ROW_HEIGHT / 8), MENU_ROW_HEIGHT / 8);
  if (item->shortDescriptionSize > 0) {
    printShortDescription(item->shortDescriptionIndex, item->shortDescriptionSize);
  }
  settingsMenuSetItemActive(active);
  item->draw();
  if (active && MENU_ROWS > 1) {
    // Highlight the active row, leaving the right-most column for the scroll indicator.
    OLED::invertArea(0, 0, OLED_WIDTH - 2, MENU_ROW_HEIGHT - 1);
  }
  OLED::resetDrawingWindow();
}

OperatingMode moveToNextEntry(guiContext *cxt) {
  uint16_t *mainEntry         = &(cxt->scratch_state.state1);
  uint16_t *subEntry          = &(cxt->scratch_state.state2);
  uint16_t *currentMenuLength = &(cxt->scratch_state.state5);
  uint16_t *isRenderingHelp   = &(cxt->scratch_state.state6);

  if (*isRenderingHelp) {
    *isRenderingHelp = 0;
    return OperatingMode::SettingsMenu;
  }

  // Remember the page position before moving so we know whether to scroll.
  const menuitem *oldMenu   = (*subEntry == 0) ? rootSettingsMenu : subSettingsMenus[*mainEntry];
  const uint16_t  oldScreen = (*subEntry == 0) ? *mainEntry : (*subEntry) - 1;
  const uint16_t  oldWindow = getWindowStart(oldMenu, oldScreen, getMenuLength(oldMenu, 128));

  *currentMenuLength = 0; // Reset menu length
  // Scroll down
  // We can increment freely _once_
  if (*subEntry == 0) {
    (*mainEntry) += 1;

    if (rootSettingsMenu[*mainEntry].draw == nullptr) {
      // We are off the end of the menu now
      saveSettings();
      cxt->transitionMode = TransitionAnimation::Left;
      return OperatingMode::HomeScreen;
    }
    // Check if visible
    if (rootSettingsMenu[*mainEntry].isVisible != nullptr && !rootSettingsMenu[*mainEntry].isVisible()) {
      // We need to move on as this one isn't visible
      return moveToNextEntry(cxt);
    }
  } else {
    (*subEntry) += 1;

    // If the new entry is null, we need to exit
    if (subSettingsMenus[*mainEntry][(*subEntry) - 1].draw == nullptr) {
      (*subEntry)         = 0; // Reset back to the main menu
      cxt->transitionMode = TransitionAnimation::Left;
      // Have to break early to avoid the below check underflowing
      return OperatingMode::SettingsMenu;
    }
    // Check if visible
    if (subSettingsMenus[*mainEntry][(*subEntry) - 1].isVisible != nullptr &&
        !subSettingsMenus[*mainEntry][(*subEntry) - 1].isVisible()) {
      // We need to move on as this one isn't visible
      return moveToNextEntry(cxt);
    }
  }

  const menuitem *newMenu   = (*subEntry == 0) ? rootSettingsMenu : subSettingsMenus[*mainEntry];
  const uint16_t  newScreen = (*subEntry == 0) ? *mainEntry : (*subEntry) - 1;
  const uint16_t  newWindow = getWindowStart(newMenu, newScreen, getMenuLength(newMenu, 128));
  if (newWindow == oldWindow) {
    // Last page: only the highlight moves, nothing scrolls.
    cxt->transitionMode = TransitionAnimation::None;
  } else if (MENU_ROWS > 1) {
    // Only slide when the visible window moved by exactly one row; a larger
    // jump (skipped invisible entries) is redrawn directly instead.
    if (newWindow == oldWindow + 1) {
      cxt->transitionMode = TransitionAnimation::RowDown;
      // Clear the outgoing top row's highlight now. The transition pre-loads
      // the incoming row from the new frame, so the highlight moves onto it
      // before the list starts sliding.
      OLED::invertArea(0, 0, OLED_WIDTH - 2, MENU_ROW_HEIGHT - 1);
    } else {
      cxt->transitionMode = TransitionAnimation::None;
    }
  } else {
    cxt->transitionMode = TransitionAnimation::Down;
  }
  return OperatingMode::SettingsMenu;
}

OperatingMode gui_SettingsMenu(const ButtonState buttons, guiContext *cxt) {
  // Render out the current settings menu
  // State 1 -> Root menu
  // State 2 -> Sub entry
  // Draw main entry if sub-entry is 0, otherwise draw sub-entry

  uint16_t *mainEntry              = &(cxt->scratch_state.state1);
  uint16_t *subEntry               = &(cxt->scratch_state.state2);
  uint32_t *autoRepeatAcceleration = &(cxt->scratch_state.state3);
  uint32_t *autoRepeatTimer        = &(cxt->scratch_state.state4);
  uint16_t *currentMenuLength      = &(cxt->scratch_state.state5);
  uint16_t *isRenderingHelp        = &(cxt->scratch_state.state6);

  const menuitem *currentMenu;
  // Draw the currently on screen item
  uint16_t currentScreen;
  if (*subEntry == 0) {
    // Drawing main menu
    currentMenu   = rootSettingsMenu;
    currentScreen = *mainEntry;
  } else {
    // Drawing sub menu
    currentMenu   = subSettingsMenus[*mainEntry];
    currentScreen = (*subEntry) - 1;
  }

  // Update the cached menu length if unknown
  if (*currentMenuLength == 0) {
    // We walk the current menu to find the length
    *currentMenuLength = getMenuLength(currentMenu, 128 /* Max length of any menu*/);
  }

  const menuitem *activeItem = &(currentMenu[currentScreen]);
  if (activeItem->description != 0 && (xTaskGetTickCount() - lastButtonTime) >= HELP_TEXT_TIMEOUT_TICKS) {
    // Help text is still drawn full screen, independent of the number of rows.
    *isRenderingHelp = 1;
    const char *description = translatedString(Tr->SettingsDescriptions[activeItem->description - 1]);
    drawScrollingText(description, (xTaskGetTickCount() - lastButtonTime) - HELP_TEXT_TIMEOUT_TICKS);
  } else {
    const uint16_t windowStart = getWindowStart(currentMenu, currentScreen, *currentMenuLength);
    for (uint8_t row = 0; row < MENU_ROWS; row++) {
      const uint16_t visiblePosition = windowStart + row;
      if (visiblePosition >= *currentMenuLength) {
        break;
      }
      const uint16_t rawIndex = getVisibleEntryIndex(currentMenu, visiblePosition);
      if (rawIndex == 0xFFFF) {
        break;
      }
      renderMenuItem(&(currentMenu[rawIndex]), row, rawIndex == currentScreen);
    }
  }

  if (*isRenderingHelp == 0 && *currentMenuLength > 0) {
    //  Draw scroll
    const uint16_t windowStart = getWindowStart(currentMenu, currentScreen, *currentMenuLength);
    const uint16_t visibleRows = (*currentMenuLength < MENU_ROWS) ? *currentMenuLength : MENU_ROWS;

    // The height of the indicator is screen res height / total menu entries
    uint16_t indicatorHeight = (OLED_HEIGHT * visibleRows) / *currentMenuLength;
    if (indicatorHeight == 0) {
      indicatorHeight = 1; // always at least 1 pixel
    }
    uint16_t position = (OLED_HEIGHT * windowStart) / *currentMenuLength;
    if (position + indicatorHeight > OLED_HEIGHT) {
      position = OLED_HEIGHT - indicatorHeight;
    }

    bool showScrollbar = true;

    // Store if its the last option for this setting
    bool isLastOptionForSetting = false;
    if ((int)currentMenu[currentScreen].autoSettingOption < (int)SettingsOptions::SettingsOptionsLength) {
      isLastOptionForSetting = isLastSettingValue(currentMenu[currentScreen].autoSettingOption);
    }

    // Last settings menu entry, reset scroll show back so it flashes
    if (isLastOptionForSetting) {
      showScrollbar = false;
    }

    // Or Flash it
    showScrollbar |= (xTaskGetTickCount() % (TICKS_SECOND / 4) < (TICKS_SECOND / 8));

    if (showScrollbar) {
      OLED::drawScrollIndicator((uint8_t)position, (uint8_t)indicatorHeight);
    }
  }

  // Now handle user button input
  auto callIncrementHandler = [&]() {
    if (currentMenu[currentScreen].incrementHandler != nullptr) {
      currentMenu[currentScreen].incrementHandler();
    } else if ((int)currentMenu[currentScreen].autoSettingOption < (int)SettingsOptions::SettingsOptionsLength) {
      nextSettingValue(currentMenu[currentScreen].autoSettingOption);
    }
    return false;
  };

  // Modify a button value before processing a key press if setting to swap buttons is enabled
  bool    swapButtonSettings = getSettingValue(SettingsOptions::ReverseButtonSettings);
  uint8_t buttonPress;
  switch (buttons) {
  case BUTTON_F_LONG:
    buttonPress = swapButtonSettings ? BUTTON_B_LONG : BUTTON_F_LONG;
    break;
  case BUTTON_F_SHORT:
    buttonPress = swapButtonSettings ? BUTTON_B_SHORT : BUTTON_F_SHORT;
    break;
  case BUTTON_B_LONG:
    buttonPress = swapButtonSettings ? BUTTON_F_LONG : BUTTON_B_LONG;
    break;
  case BUTTON_B_SHORT:
    buttonPress = swapButtonSettings ? BUTTON_F_SHORT : BUTTON_B_SHORT;
    break;
  default:
    buttonPress = buttons;
    break;
  }

  OperatingMode newMode = OperatingMode::SettingsMenu;
  switch (buttonPress) {
  case BUTTON_NONE:
    (*autoRepeatAcceleration) = 0; // reset acceleration
    (*autoRepeatTimer)        = 0; // reset acceleration
    break;
  case BUTTON_BOTH:
    if (*subEntry == 0) {
      saveSettings();
      cxt->transitionMode = TransitionAnimation::Left;
      return OperatingMode::HomeScreen;
    } else {
      cxt->transitionMode = TransitionAnimation::Left;
      *subEntry           = 0;
      return OperatingMode::SettingsMenu;
    }
    break;
  case BUTTON_F_LONG:
    if (xTaskGetTickCount() + (*autoRepeatAcceleration) > (*autoRepeatTimer) + PRESS_ACCEL_INTERVAL_MAX) {
      callIncrementHandler();
      // Update the check for if its the last version
      bool isLastOptionForSetting = false;
      if ((int)currentMenu[currentScreen].autoSettingOption < (int)SettingsOptions::SettingsOptionsLength) {
        isLastOptionForSetting = isLastSettingValue(currentMenu[currentScreen].autoSettingOption);
      }

      if (isLastOptionForSetting) {
        (*autoRepeatTimer) = TICKS_SECOND * 2;
      } else {
        (*autoRepeatTimer) = 0;
      }
      (*autoRepeatTimer) += xTaskGetTickCount();
      (*autoRepeatAcceleration) += PRESS_ACCEL_STEP;
      *currentMenuLength = 0; // Reset incase menu visible changes
    }
    break;
  case BUTTON_F_SHORT:
    // Increment setting
    if (*isRenderingHelp) {
      *isRenderingHelp = 0;
    } else {
      *currentMenuLength = 0; // Reset incase menu visible changes
      if (*subEntry == 0) {
        // In a root menu, if its null handler we enter the menu
        if (currentMenu[currentScreen].incrementHandler != nullptr) {
          currentMenu[currentScreen].incrementHandler();
        } else {
          (*subEntry) += 1;
          cxt->transitionMode = TransitionAnimation::Right;
        }
      } else {
        callIncrementHandler();
      }
    }
    break;
  case BUTTON_B_LONG:
    if (xTaskGetTickCount() + (*autoRepeatAcceleration) > (*autoRepeatTimer) + PRESS_ACCEL_INTERVAL_MAX) {
      (*autoRepeatTimer) = xTaskGetTickCount();
      (*autoRepeatAcceleration) += PRESS_ACCEL_STEP;
    } else {
      break;
    }
    /* Fall through*/
  case BUTTON_B_SHORT:
    // Increment menu item
    newMode = moveToNextEntry(cxt);
    break;
  default:
    break;
  }

  if ((PRESS_ACCEL_INTERVAL_MAX - (*autoRepeatAcceleration)) < PRESS_ACCEL_INTERVAL_MIN) {
    (*autoRepeatAcceleration) = PRESS_ACCEL_INTERVAL_MAX - PRESS_ACCEL_INTERVAL_MIN;
  }

  // Otherwise we stay put for next render iteration
  return newMode;
}
