#include "BSP.h"
#include "BSP_Power.h"
#include "Pins.h"
#include "QC3.h"
#include "STUSB4500.hpp"
#include "Settings.h"
#include "USBPD.h"
#include "configuration.h"
#include "stm32f1xx_hal.h"

void power_check() {
#ifdef POW_PD_STUSB4500
  if (STUSB4500::has_negotiated()) {
    return; // We are using PD
  }
#endif
}

bool getIsPoweredByDCIN() {
#ifdef POW_PD_STUSB4500
  return !STUSB4500::is_vbus_ready();
#endif
  return true;
}
