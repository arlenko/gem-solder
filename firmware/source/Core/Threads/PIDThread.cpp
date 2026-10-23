/*
 * PIDThread.cpp
 *
 *  Created on: 29 May 2020
 *      Author: Ralim
 */

#include "BSP.h"
#include "FreeRTOS.h"
#include "Settings.h"
#include "TipThermoModel.h"
#include "cmsis_os.h"
#include "configuration.h"
#include "expMovingAverage.h"
#include "history.hpp"
#include "main.hpp"
#include "power.hpp"
#include "task.h"

#ifdef POW_PD
#if POW_PD == 1
#include "USBPD.h"
#endif
#endif

#ifdef POW_PD_STUSB4500
#include "STUSB4500.hpp"
#endif

static TickType_t          powerPulseWaitUnit      = 25 * TICKS_100MS;      // 2.5 s
static TickType_t          powerPulseDurationUnit  = (5 * TICKS_100MS) / 2; // 250 ms
TaskHandle_t               pidTaskNotification     = NULL;
volatile TemperatureType_t currentTempTargetDegC   = 0; // Current temperature target in C
int32_t                    powerSupplyWattageLimit = 0;

static volatile bool thermalRunawayLatched = false;

static int32_t getPIDResultX10Watts(TemperatureType_t set_point, TemperatureType_t current_value);
static void    detectThermalRunaway(const TemperatureType_t currentTipTempInC, const int32_t x10WattsApplied);
static int32_t setOutputx10WattsViaFilters(int32_t x10Watts);

bool thermalRunawayTripped(void) { return thermalRunawayLatched; }

#ifdef PID_DEBUG
volatile int32_t PIDDebugResults[3] = {0, 0, 0};
#endif

/* StartPIDTask function */
void startPIDTask(void const *argument __unused) {
  /*
   * We take the current tip temperature & evaluate the next step for the tip
   * control PWM.
   */
  setTipX10Watts(0); // disable the output at startup

  currentTempTargetDegC = 0; // Force start with no output (off). If in sleep / soldering this will
                             // be over-ridden rapidly

  pidTaskNotification = xTaskGetCurrentTaskHandle();

  TemperatureType_t PIDTempTarget = 0;
  // Pre-seed the adc filters
  for (int i = 0; i < 32; i++) {
    ulTaskNotifyTake(pdTRUE, 5);
    TipThermoModel::getTipInC(true);
    getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 1);
  }

  while (preStartChecks() == 0) {
    resetWatchdog();
    ulTaskNotifyTake(pdTRUE, 2000);
  }
// Wait for PD if its in the middle of negotiation
#ifdef POW_PD
#if POW_PD == 1
  // This is an FUSB based PD capable device
  // Wait up to 3 seconds for USB-PD to settle
  while (USBPowerDelivery::negotiationInProgress() && xTaskGetTickCount() < (TICKS_SECOND * 3)) {
    resetWatchdog();
    ulTaskNotifyTake(pdTRUE, TICKS_100MS);
  }
#endif
#endif
#ifdef POW_PD_STUSB4500
  while (!STUSB4500::has_negotiated() && xTaskGetTickCount() < TICKS_SECOND * 2) {
    resetWatchdog();
    ulTaskNotifyTake(pdTRUE, TICKS_100MS);
  }
#endif

  int32_t x10WattsOut = 0;

  for (;;) {
    x10WattsOut = 0;
    // This is a call to block this thread until the ADC does its samples
    if (ulTaskNotifyTake(pdTRUE, TICKS_SECOND * 2)) {
      // Do the reading here to keep the temp calculations churning along
      TemperatureType_t currentTipTempInC = TipThermoModel::getTipInC(true);

      PIDTempTarget = currentTempTargetDegC;
      if (PIDTempTarget > 0) {
        // Cap the max set point to 450C
        if (PIDTempTarget > 450) {
          // Maximum allowed output
          PIDTempTarget = 450;
        }
        // Safety check that not aiming higher than current tip can measure
        if (PIDTempTarget > TipThermoModel::getTipMaxInC()) {
          PIDTempTarget = TipThermoModel::getTipMaxInC();
        }

        x10WattsOut = getPIDResultX10Watts(PIDTempTarget, currentTipTempInC);
      }
      // Apply the output, then feed the detector the power that was actually applied
      const int32_t x10WattsApplied = setOutputx10WattsViaFilters(x10WattsOut);
      detectThermalRunaway(currentTipTempInC, x10WattsApplied);
    } else {
      // ADC interrupt timeout
      setTipPWM(0, false);
    }
#ifdef DEBUG_UART_OUTPUT
    log_system_state(x10WattsOut);
#endif
  }
}

template <class T, T Kp, T Ki, T Kd> struct PID {
  T previous_error_term;
  T integration_running_sum;
  // Filter Kd result to reduce power swings caused by temperature measurement noise amplification
  expMovingAverage<T, 48> kd_result_filter = {0};

  T update(const T set_point, const T new_reading, const TickType_t interval_ms, const T max_output) {
    const T target_delta = set_point - new_reading;

    // Proportional term
    const T kp_result = Kp * target_delta;
    // Integral probe before anti-windup decision
    T ki_probe = integration_running_sum / 100;
    // Derivative term
    T derivative        = (target_delta - previous_error_term);
    T kd_result_instant = ((Kd * derivative) / (T)(interval_ms));
    kd_result_filter.update(kd_result_instant);
    T kd_result = kd_result_filter.average();

    T output_probe = kp_result + ki_probe + kd_result;

    // Integral term as we use mixed sampling rates, we cant assume a constant sample interval
    // Thus we multiply this out by the interval time to ~= dv/dt
    // Then the shift by 1000 is ms -> Seconds.
    // In order to prevent windup we don't accumulate integrator when the output is already saturated
    bool is_saturated = (output_probe >= max_output && target_delta > 0) || (output_probe <= 0 && target_delta < 0);
    if (!is_saturated) {
      integration_running_sum += (target_delta * (T)interval_ms * Ki) / 1000;
    }

    // Calculate the integral term, we use a shift 100 to get precision in integral as we often need small amounts
    T ki_result = integration_running_sum / 100;

    // Summation of the outputs
    T output = kp_result + ki_result + kd_result;

#ifdef PID_DEBUG
    PIDDebugResults[0] = kp_result;
    PIDDebugResults[1] = ki_result;
    PIDDebugResults[2] = kd_result;
#endif

    // Restrict to max / 0
    if (output > max_output) {
      output = max_output;
    } else if (output < 0) {
      output = 0;
    }

    // Save target_delta to previous target_delta
    previous_error_term = target_delta;

    return output;
  }
};

int32_t getPIDResultX10Watts(TemperatureType_t set_point, TemperatureType_t current_reading) {
  static TickType_t                                                 lastCall = 0;
  static PID<TemperatureType_t, TIP_PID_KP, TIP_PID_KI, TIP_PID_KD> pid      = {0, 0};
  const TickType_t                                                  interval = (xTaskGetTickCount() - lastCall);

  lastCall = xTaskGetTickCount();

  return pid.update(set_point, current_reading, interval, getX10WattageLimits());
}

/*
 * Detection of thermal runaway
 * The goal of this is to handle cases where something has gone wrong:
 * 1. The tip MOSFET is broken, so power is being constantly applied to the tip
 * a. This shows as the temperature rising while the heater is commanded off
 * b. And eventually as the reading being pinned at the top of the ADC range
 * 2. Broken temperature sense
 * a. Temp is stuck at a value, so it does not respond while heating
 * b. Or it reads pinned at the top of its range
 * Any of these latches a fault; the output is gated off in setOutputx10WattsViaFilters and
 * the UI stays in the ThermalRunaway warning.
 */
void detectThermalRunaway(const TemperatureType_t currentTipTempInC, const int32_t x10WattsApplied) {
  static struct {
    TickType_t        heatStart;       // 0 => not currently hard-heating
    TemperatureType_t minTempInWindow; // Coldest reading in the current hard-heating window
    TemperatureType_t maxTempInWindow; // Hottest reading in the current hard-heating window
    TickType_t        offStart;        // 0 => heater currently on
    TemperatureType_t offTemp;         // Reading when the heater was last commanded off
    TemperatureType_t offFloor;        // Low-water mark once the tip has settled after heating
    bool              offSettled;
    TickType_t        peggedStart;     // 0 => reading not pinned
  } runaway = {};

  if (thermalRunawayLatched) {
    return; // Decision made; the output gate keeps the heater off from here on
  }

  const TickType_t now     = xTaskGetTickCount();
  const bool       heating = x10WattsApplied > 0;

  // 1b/2b: reading pinned at the top of the ADC range
  if (getTipRawTemp(0) >= THERMAL_RUNAWAY_PEGGED_RAW) {
    if (runaway.peggedStart == 0) {
      runaway.peggedStart = now;
    } else if ((now - runaway.peggedStart) >= (THERMAL_RUNAWAY_PEGGED_SEC * TICKS_SECOND)) {
      thermalRunawayLatched = true;
      return;
    }
  } else {
    runaway.peggedStart = 0;
  }

  // 2a: sustained heating with no temperature response. Only meaningful while the PID is asking
  // for close to everything the supply can give: at steady state the tip legitimately holds within
  // a few degrees of the set point. Re-arms after a healthy window so this keeps testing for the
  // whole session instead of latching once at boot.
  const bool hardHeating = heating && (x10WattsApplied >= (getX10WattageLimits() * 3) / 4);
  if (hardHeating) {
    if (runaway.heatStart == 0) { // Rising edge into a hard heating burst
      runaway.heatStart       = now;
      runaway.minTempInWindow = currentTipTempInC;
      runaway.maxTempInWindow = currentTipTempInC;
    }
    if (currentTipTempInC < runaway.minTempInWindow) {
      runaway.minTempInWindow = currentTipTempInC;
    }
    if (currentTipTempInC > runaway.maxTempInWindow) {
      runaway.maxTempInWindow = currentTipTempInC;
    }
    if ((now - runaway.heatStart) >= (THERMAL_RUNAWAY_TIME_SEC * TICKS_SECOND)) {
      if ((runaway.maxTempInWindow - runaway.minTempInWindow) < THERMAL_RUNAWAY_TEMP_C) {
        thermalRunawayLatched = true;
        return;
      }
      runaway.heatStart       = now;
      runaway.minTempInWindow = currentTipTempInC;
      runaway.maxTempInWindow = currentTipTempInC;
    }
  } else {
    runaway.heatStart = 0;
  }

  // 1a: temperature climbing while the heater is commanded off
  if (heating) {
    runaway.offStart   = 0;
    runaway.offSettled = false;
  } else if (runaway.offStart == 0) { // Falling edge out of a heating burst
    runaway.offStart = now;
    runaway.offTemp  = currentTipTempInC;
  } else if (!runaway.offSettled && (now - runaway.offStart) >= (THERMAL_RUNAWAY_SETTLE_SEC * TICKS_SECOND)) {
    // Thermal lag has finished; from here the tip can only get cooler
    runaway.offSettled = true;
    runaway.offFloor   = currentTipTempInC;
    // A rise this large cannot be thermal lag: the heater is stuck on. This also catches the case
    // where the reading hit its ceiling during the settle window before a floor could be taken.
    if (currentTipTempInC > (runaway.offTemp + (THERMAL_RUNAWAY_RISE_C * 2))) {
      thermalRunawayLatched = true;
    }
  } else if (runaway.offSettled) {
    if (currentTipTempInC < runaway.offFloor) {
      runaway.offFloor = currentTipTempInC;
    } else if (currentTipTempInC > (runaway.offFloor + THERMAL_RUNAWAY_RISE_C)) {
      thermalRunawayLatched = true;
    }
  }
}

int32_t setOutputx10WattsViaFilters(int32_t x10WattsOut) {
  static TickType_t lastPowerPulseStart = 0;
  static TickType_t lastPowerPulseEnd   = 0;
#ifdef SLEW_LIMIT
  static int32_t x10WattsOutLast = 0;
#endif

  // If the user turns on the option of using an occasional pulse to keep the power bank on
  if (getSettingValue(SettingsOptions::KeepAwakePulse)) {
    const TickType_t powerPulseWait = powerPulseWaitUnit * getSettingValue(SettingsOptions::KeepAwakePulseWait);
    if (xTaskGetTickCount() - lastPowerPulseStart > powerPulseWait) {
      const TickType_t powerPulseDuration =
          powerPulseDurationUnit * getSettingValue(SettingsOptions::KeepAwakePulseDuration);
      lastPowerPulseStart = xTaskGetTickCount();
      lastPowerPulseEnd   = lastPowerPulseStart + powerPulseDuration;
    }

    // If current PID is less than the pulse level, check if we want to constrain to the pulse as the floor
    if (x10WattsOut < getSettingValue(SettingsOptions::KeepAwakePulse) && xTaskGetTickCount() < lastPowerPulseEnd) {
      x10WattsOut = getSettingValue(SettingsOptions::KeepAwakePulse);
    }
  }

  // Secondary safety check to forcefully disable header when within ADC noise of top of ADC
  if (getTipRawTemp(0) > (0x7FFF - 32)) {
    x10WattsOut = 0;
  }
  if (thermalRunawayLatched) {
    x10WattsOut = 0;
  }
#ifdef SLEW_LIMIT
  if (x10WattsOut - x10WattsOutLast > SLEW_LIMIT) {
    x10WattsOut = x10WattsOutLast + SLEW_LIMIT;
  }
  if (x10WattsOut < 0) {
    x10WattsOut = 0;
  }
  x10WattsOutLast = x10WattsOut;
#endif
  setTipX10Watts(x10WattsOut);
  resetWatchdog();
  return x10WattsOut;
}
