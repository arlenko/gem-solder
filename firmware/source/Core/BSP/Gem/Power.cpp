#include "BSP.h"
#include "BSP_Power.h"
#include "Pins.h"
#include "QC3.h"
#include "STUSB4500.hpp"
#include "Settings.h"
#include "USBPD.h"
#include "configuration.h"
#include "stm32f1xx_hal.h"

#ifdef POW_PD_STUSB4500
// Cached source classification, refreshed once per power_check() cycle.
// getIsPoweredByDCIN() is read several times per UI frame (undervoltage
// check, power-source icon), so keep the bit-banged I2C reads out of that
// path and let every caller within a frame agree on the same value.
static volatile bool usbSourcePresent = false;
#endif

void power_check() {
#ifdef POW_PD_STUSB4500
  // The STUSB4500 is powered from VBUS, so on DC-only it is unpowered and both
  // reads below fail (-> false, i.e. DC). is_attached() is a state bit that
  // holds through PD voltage transitions, while is_vbus_ready() is a level bit
  // that can blink mid-transition, so accept either.
  usbSourcePresent = STUSB4500::is_attached() || STUSB4500::is_vbus_ready();
#endif
}

bool getIsPoweredByDCIN() {
#ifdef POW_PD_STUSB4500
  return !usbSourcePresent;
#endif
  return true;
}
