#include "Buttons.hpp"
#include "OperatingModeUtilities.h"

TickType_t        lastHallEffectSleepStart = 0;
extern TickType_t lastMovementTime;

bool shouldBeSleeping() {
#ifndef NO_SLEEP_MODE
  // Return true if the iron should be in sleep mode
  if (getSettingValue(SettingsOptions::Sensitivity) && getSettingValue(SettingsOptions::SleepTime)) {
    // In auto start we are asleep until movement
    if (lastMovementTime == 0 && lastButtonTime == 0) {
      return true;
    }
    if (lastMovementTime > 0 || lastButtonTime > 0) {
      if (((xTaskGetTickCount() - lastMovementTime) > getSleepTimeout()) && ((xTaskGetTickCount() - lastButtonTime) > getSleepTimeout())) {
        return true;
      }
    }
  }

#ifdef HALL_SENSOR
  // If the hall effect sensor is enabled in the build, check if its over
  // threshold, and if so then we force sleep
  if (getHallSensorFitted() && lookupHallEffectThreshold()) {
    int16_t hallEffectStrength = getRawHallEffect();
    if (hallEffectStrength < 0) {
      hallEffectStrength = -hallEffectStrength;
    }
    // Have absolute value of measure of magnetic field strength
    if (hallEffectStrength > lookupHallEffectThreshold()) {
      if (lastHallEffectSleepStart == 0) {
        lastHallEffectSleepStart = xTaskGetTickCount();
      }
      if ((xTaskGetTickCount() - lastHallEffectSleepStart) > getHallEffectSleepTimeout()) {
        return true;
      }
    } else {
      lastHallEffectSleepStart = 0;
    }
  }
#endif

#ifdef STAND_SENSE
  // Enable sleep when tip touching a plate.
  // Debounce to avoild false triggers.
  static TickType_t lastStandSenseStart = 0;
  if (HAL_GPIO_ReadPin(STAND_SENSE_GPIO_Port, STAND_SENSE_Pin) == GPIO_PIN_RESET) {
    if (lastStandSenseStart == 0) {
      lastStandSenseStart = xTaskGetTickCount();
    }
    if ((xTaskGetTickCount() - lastStandSenseStart) > TICKS_100MS) {
      return true;
    }

  } else {
    lastStandSenseStart = 0;
  }
#endif
#endif // ndef NO_SLEEP_MODE
  return false;
}
