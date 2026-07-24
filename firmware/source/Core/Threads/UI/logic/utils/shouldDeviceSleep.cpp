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
      if (((xTaskGetTickCount() - lastMovementTime) > getSleepTimeout()) &&
          ((xTaskGetTickCount() - lastButtonTime) > getSleepTimeout())) {
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
  // Enable sleep mode when handle touching a metal plate pulling stand sense pin low.
  // Quick tap toggles sleep mode when tap to sleep feature enabled
  GPIO_PinState        pinState           = HAL_GPIO_ReadPin(STAND_SENSE_GPIO_Port, STAND_SENSE_Pin);
  uint32_t             now                = HAL_GetTick();
  bool                 tapToSleepEnabled  = getSettingValue(SettingsOptions::TapToSleep);
  static TickType_t    lastPinStateChange = 0;
  static GPIO_PinState prevPinState       = pinState;
  static bool          tapSleep           = false;

  if (pinState != prevPinState) {
    if (pinState == GPIO_PIN_SET && (now - lastPinStateChange) < TICKS_100MS * 2) {
      tapSleep = !tapSleep;
    }
    lastPinStateChange = HAL_GetTick();
  }
  prevPinState = pinState;

  return pinState == GPIO_PIN_RESET || (tapToSleepEnabled && tapSleep);
#endif
#endif // ndef NO_SLEEP_MODE
  return false;
}
