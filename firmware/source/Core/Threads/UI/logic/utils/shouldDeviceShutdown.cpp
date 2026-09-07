#include "Buttons.hpp"
#include "OperatingModeUtilities.h"

extern TickType_t lastMovementTime;
extern TickType_t lastHallEffectSleepStart;
extern TickType_t lastStandSenseSleepStart;

bool shouldShutdown(void) {
  if (getSettingValue(SettingsOptions::ShutdownTime)) { // only allow shutdown exit if time > 0
#ifndef NO_ACCEL
    if (lastMovementTime) {
      if (((TickType_t)(xTaskGetTickCount() - lastMovementTime)) >
          (TickType_t)(getSettingValue(SettingsOptions::ShutdownTime) * TICKS_MIN)) {
        return true;
      }
    }
#endif
#ifdef HALL_SENSOR
    if (lastHallEffectSleepStart) {
      if (((TickType_t)(xTaskGetTickCount() - lastHallEffectSleepStart)) >
          (TickType_t)(getSettingValue(SettingsOptions::ShutdownTime) * TICKS_MIN)) {
        return true;
      }
    }
#endif
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
