#include "FS2711.hpp"
#include "HUB238.hpp"
#include "OperatingModes.h"
#include "ui_drawing.hpp"
OperatingMode showWarnings(const ButtonState buttons, guiContext *cxt) {
  // Display alert if settings were reset

  switch (cxt->scratch_state.state1) {
  case 0: // Settings reset warning
    if (settingsWereReset) {
      if (warnUser(translatedString(Tr->SettingsResetMessage), buttons)) {
        settingsWereReset         = false;
        cxt->scratch_state.state1 = 1;
      }
    } else {
      cxt->scratch_state.state1 = 1;
    }
    break;
  case 1: // Device validations
#ifdef DEVICE_HAS_VALIDATION_SUPPORT
    if (getDeviceValidationStatus()) {
      // Warn user this device might be counterfeit
      if (warnUser(translatedString(Tr->DeviceFailedValidationWarning), buttons)) {
        cxt->scratch_state.state1 = 2;
      }
    } else {
      cxt->scratch_state.state1 = 2;
    }
#else
    cxt->scratch_state.state1 = 2;
#endif
    break;
  case 2:
#ifdef POW_PD
    // We expect pd to be present
    if (!USBPowerDelivery::fusbPresent()) {
      if (getSettingValue(SettingsOptions::PDMissingWarningCounter) < 2) {
        if (warnUser(translatedString(Tr->NoPowerDeliveryMessage), buttons)) {
          nextSettingValue(SettingsOptions::PDMissingWarningCounter);
          saveSettings();
          cxt->scratch_state.state1 = 3;
        }
      } else {
        cxt->scratch_state.state1 = 3;
      }
    } else {
      cxt->scratch_state.state1 = 3;
    }
#else
#if POW_PD_EXT == 1
    if (!hub238_probe()) {
      if (getSettingValue(SettingsOptions::PDMissingWarningCounter) < 2) {
        if (warnUser(translatedString(Tr->NoPowerDeliveryMessage), buttons)) {
          cxt->scratch_state.state1 = 3;
          nextSettingValue(SettingsOptions::PDMissingWarningCounter);
          saveSettings();
        }
      } else {
        cxt->scratch_state.state1 = 3;
      }
    } else {
      cxt->scratch_state.state1 = 3;
    }
#else
#if POW_PD_EXT == 2
    if (!FS2711::probe()) {
      if (getSettingValue(SettingsOptions::PDMissingWarningCounter) < 2) {
        if (warnUser(translatedString(Tr->NoPowerDeliveryMessage), buttons)) {
          cxt->scratch_state.state1 = 3;
          nextSettingValue(SettingsOptions::PDMissingWarningCounter);
          saveSettings();
        }
      } else {
        cxt->scratch_state.state1 = 3;
      }
    } else {
      cxt->scratch_state.state1 = 3;
    }
#else
    cxt->scratch_state.state1 = 3;
#endif /*POW_PD_EXT==1*/
#endif /*POW_PD_EXT==2*/
#endif /*POW_PD*/

    break;
  default:
    // We are off the end, warnings done
    return OperatingMode::StartupLogo;
  }

  return OperatingMode::StartupWarnings; // Stay in warnings
}
