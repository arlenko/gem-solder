#include "OperatingModes.h"
#include "ui_drawing.hpp"

OperatingMode showDebugMenu(const ButtonState buttons, guiContext *cxt) {

  ui_draw_debug_menu(cxt->scratch_state.state1);

  if (buttons == BUTTON_B_SHORT) {
    cxt->transitionMode = TransitionAnimation::Up;
    return OperatingMode::HomeScreen;
  } else if (buttons == BUTTON_F_SHORT) {
    cxt->scratch_state.state1++;
#ifdef NO_ACCEL
    if (cxt->scratch_state.state1 == 9) {
      cxt->scratch_state.state1 = 10;
    }
    if (cxt->scratch_state.state1 == 14) {
      cxt->scratch_state.state1 = 15;
    }
#endif
#ifdef HALL_SENSOR
    cxt->scratch_state.state1 = cxt->scratch_state.state1 % 17;
#else
    cxt->scratch_state.state1 = cxt->scratch_state.state1 % 16;
#endif
  }
  return OperatingMode::DebugMenuReadout; // Stay in debug menu
}
