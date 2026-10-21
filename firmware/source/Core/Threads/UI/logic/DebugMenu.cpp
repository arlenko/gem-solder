#include "OperatingModes.h"
#include "ui_drawing.hpp"

OperatingMode showDebugMenu(const ButtonState buttons, guiContext *cxt) {

  ui_draw_debug_menu(cxt->scratch_state.state1);

  if (buttons == BUTTON_B_SHORT) {
    cxt->transitionMode = TransitionAnimation::Up;
    return OperatingMode::HomeScreen;
#if defined(HAS_POWER_DEBUG_MENU) && defined(PD_DEBUG_ENTER_FROM_SYSTEM_DEBUG)
  } else if (buttons == BUTTON_B_LONG) {
    cxt->transitionMode = TransitionAnimation::Down;
    return OperatingMode::UsbPDDebug;

#endif
  } else if (buttons == BUTTON_F_SHORT) {
    cxt->scratch_state.state1++;
    cxt->scratch_state.state1 = cxt->scratch_state.state1 % 14;
  }
  return OperatingMode::DebugMenuReadout; // Stay in debug menu
}
