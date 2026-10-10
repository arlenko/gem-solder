//
// Created by laura on 24.04.23.
//

#include "SolderingCommon.h"
#include "OperatingModes.h"
#include "STUSB4500.hpp"
#include "Types.h"
#include "configuration.h"
#include "history.hpp"
#include "ui_drawing.hpp"

extern uint8_t heaterThermalRunawayCounter;

bool checkExitSoldering(void) {
  if (isTipDisconnected()) {
    return true; // How are we going to solder without the tip?
  }

#ifdef POW_DC
  // Undervoltage test
  if (checkForUnderVoltage()) {
    lastButtonTime = xTaskGetTickCount();
    return true;
  }
#endif

#ifdef ACCEL_EXITS_ON_MOVEMENT
  // If the accel works in reverse where movement will cause exiting the soldering mode
  if (getSettingValue(Sensitivity)) {
    if (lastMovementTime) {
      if (lastMovementTime > TICKS_SECOND * 10) {
        // If we have moved recently; in the last second
        // Then exit soldering mode

        // Movement occurred in last update
        if (((TickType_t)(xTaskGetTickCount() - lastMovementTime)) < (TickType_t)(TICKS_SECOND / 5)) {
          currentTempTargetDegC = 0;
          lastMovementTime      = 0;
          return true;
        }
      }
    }
  }
#endif

  // If we have tripped thermal runaway, turn off heater and show warning

  return false;
}

int8_t getPowerSourceNumber(void) {
  int8_t sourceNumber = 0;
  if (getIsPoweredByDCIN()) {
    sourceNumber = 0;
  } else {
    // We are not powered via DC, so want to display the appropriate state for PD or QC
    bool poweredbyPD        = false;
    bool pdHasVBUSConnected = false;
#ifdef POW_PD
    if (USBPowerDelivery::fusbPresent()) {
      // We are PD capable
      if (USBPowerDelivery::negotiationComplete()) {
        // We are powered via PD
        poweredbyPD = true;
#ifdef VBUS_MOD_TEST
        pdHasVBUSConnected = USBPowerDelivery::isVBUSConnected();
#endif
      }
    }
#endif
#ifdef POW_PD_STUSB4500
    if (STUSB4500::has_negotiated()) {
      poweredbyPD        = true;
      pdHasVBUSConnected = true;
    }
#endif
    if (poweredbyPD) {
      if (pdHasVBUSConnected) {
        sourceNumber = 2;
      } else {
        sourceNumber = 3;
      }
    } else {
      sourceNumber = 1;
    }
  }
  return sourceNumber;
}

#ifdef POW_DC
uint8_t getDCPackLevel(void) {
  // Charge level of the lithium pack on the DC input, 1 (flat) .. 10 (full).
  uint8_t  cellCount = getSettingValue(SettingsOptions::MinDCVoltageCells) + 2;
  uint32_t cellV     = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0) / cellCount;
  // Should give us approx cell voltage X10
  // Range is 42 -> Minimum voltage setting (MinVoltageCells) = 9 steps therefore we will use battery 0-9
  if (cellV < getSettingValue(SettingsOptions::MinVoltageCells)) {
    cellV = getSettingValue(SettingsOptions::MinVoltageCells);
  }
  cellV -= getSettingValue(SettingsOptions::MinVoltageCells); // Should leave us a number of 0-9
  if (cellV > 9) {
    cellV = 9;
  }
  return cellV + 1;
}
#endif

// Returns temperature of the tip in *C/*F (based on user settings)
TemperatureType_t getTipTemp(void) {
#ifdef FILTER_DISPLAYED_TIP_TEMP
  static history<TemperatureType_t, FILTER_DISPLAYED_TIP_TEMP> Filter_Temp;
  TemperatureType_t                                            reading =
      getSettingValue(SettingsOptions::TemperatureInF) ? TipThermoModel::getTipInF() : TipThermoModel::getTipInC();
  Filter_Temp.update(reading);
  return Filter_Temp.average();

#else
  return getSettingValue(SettingsOptions::TemperatureInF) ? TipThermoModel::getTipInF() : TipThermoModel::getTipInC();
#endif
}
