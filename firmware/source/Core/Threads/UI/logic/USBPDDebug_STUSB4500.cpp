#include "OperatingModes.h"
#include "STUSB4500.hpp"
#include "ui_drawing.hpp"
#ifdef POW_PD_STUSB4500
#ifdef HAS_POWER_DEBUG_MENU
OperatingMode showPDDebug(const ButtonState buttons, guiContext *cxt) {
  // Print out the USB-PD state
  // Basically this is like the Debug menu, but instead we want to print out the PD status
  uint16_t *screen = &(cxt->scratch_state.state1);

  if (*screen == 0) {
    // Print the PD Debug state
    stusb_debug_state_t debugState = STUSB4500::debug_get_state();
    uint8_t             vbusState  = debugState.vbus_ready ? 1 : 2;
    ui_draw_usb_pd_debug_state(vbusState, debugState.pe_state);
  } else {
    uint8_t                         capsCount = 0;
    const STUSB_PD_SRC_PDO_TypeDef *lastCaps  = STUSB4500::get_last_seen_capabilities(&capsCount);
    if (((*screen) - 1) < capsCount) {
      const STUSB_PD_SRC_PDO_TypeDef pdo         = lastCaps[(*screen) - 1];
      uint16_t                       minVoltage  = 0;
      uint16_t                       maxVoltage  = 0;
      uint16_t                       currentx100 = 0;
      uint16_t                       wattage     = 0;

      switch ((pdo.d32 >> 30) & 0b11) {
      case 0:                                        // Fixed supply
        maxVoltage  = pdo.fix.Voltage / 20;          // 50mV units -> V
        currentx100 = pdo.fix.Max_Operating_Current; // 10mA units (matches draw helper)
        break;
      case 1: // Variable supply (non-battery)
        minVoltage  = pdo.var.Min_Voltage / 20;
        maxVoltage  = pdo.var.Max_Voltage / 20;
        currentx100 = pdo.var.Operating_Current;
        break;
      case 2: // Battery supply
        minVoltage = pdo.bat.Min_Voltage / 20;
        maxVoltage = pdo.bat.Max_Voltage / 20;
        wattage    = pdo.bat.Operating_Power / 4; // 250mW units -> W
        break;
      default:
        break;
      }

      if (maxVoltage == 0) {
        (*screen) += 1; // Skip entries we can't display; auto-advance (same as FS2711)
      } else {
        ui_draw_usb_pd_debug_pdo(*screen, minVoltage, maxVoltage, currentx100, wattage);
      }
    } else {
      (*screen) = 0;
    }
  }

  if (buttons == BUTTON_B_SHORT) {
    if (cxt->previousMode == OperatingMode::DebugMenuReadout) {
      cxt->transitionMode = TransitionAnimation::Up;
    }
    return cxt->previousMode;
  } else if (buttons == BUTTON_F_SHORT) {
    *screen += 1;
  }

  return OperatingMode::UsbPDDebug;
}
#endif
#endif
