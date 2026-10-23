#include "Buttons.hpp"
#include "OperatingModeUtilities.h"

extern TickType_t lastStandSenseSleepStart;

bool shouldShutdown(void) {
  if (getSettingValue(SettingsOptions::ShutdownTime)) { // only allow shutdown exit if time > 0
#ifdef STAND_SENSE
    if (lastStandSenseSleepStart) {
      if (((TickType_t)(xTaskGetTickCount() - lastStandSenseSleepStart)) >
          (TickType_t)(getSettingValue(SettingsOptions::ShutdownTime) * TICKS_MIN)) {
        return true;
      }
    }
#endif
  }
  if (getButtonState() == BUTTON_B_LONG) { // allow also if back button is pressed long
    return true;
  }
  return false;
}
