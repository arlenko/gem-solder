#include "BSP.h"
#include "Buttons.hpp"
#include "OperatingModeUtilities.h"

TickType_t        lastHallEffectSleepStart = 0;
TickType_t        lastStandSenseSleepStart = 0;
extern TickType_t lastMovementTime;
static bool       inTapSleep = false;

void clearTapToSleep(void) { inTapSleep = false; }

bool shouldBeSleeping() {
#ifndef NO_SLEEP_MODE
#ifndef NO_ACCEL
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
#endif

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
  const uint16_t       senseVoltageThresholdmV = 2800;
  GPIO_PinState        pinState = getStandSenseVoltagemV() < senseVoltageThresholdmV ? GPIO_PIN_RESET : GPIO_PIN_SET;
  bool                 tapToSleepEnabled  = getSettingValue(SettingsOptions::TapToSleep);
  uint32_t             now                = HAL_GetTick();
  static TickType_t    lastPinStateChange = 0;
  static GPIO_PinState prevPinState       = GPIO_PIN_SET;

  if (pinState != prevPinState) {
    if (pinState == GPIO_PIN_SET && (now - lastPinStateChange) < TICKS_100MS * 2.5) {
      inTapSleep = !inTapSleep;
    }
    lastPinStateChange = HAL_GetTick();
  }
  prevPinState = pinState;

  // The user may enable tap sleep while putting the handle into a stand.
  // If the handle stays in stand for more than 1s then clean tap sleep flag.
  if (inTapSleep && pinState == GPIO_PIN_RESET && ((now - lastPinStateChange) >= TICKS_SECOND)) {
    inTapSleep = false;
  }

  const bool sleepNow = pinState == GPIO_PIN_RESET || (tapToSleepEnabled && inTapSleep);

  if (sleepNow) {
    if (lastStandSenseSleepStart == 0) {
      lastStandSenseSleepStart = xTaskGetTickCount();
    }
  } else {
    lastStandSenseSleepStart = 0;
  }

  return sleepNow;
#endif
#endif // ndef NO_SLEEP_MODE
  return false;
}
