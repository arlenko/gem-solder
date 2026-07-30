/*
 * POWThread.cpp
 *
 *  Created on: 16 Jan 2021
 *      Author: Ralim
 */

#include "BSP.h"
#include "FS2711.hpp"
#include "FreeRTOS.h"
#include "HUB238.hpp"
#include "QC3.h"
#include "STUSB4500.hpp"
#include "Settings.h"
#include "USBPD.h"
#include "cmsis_os.h"
#include "configuration.h"
#include "main.hpp"
#include "stdbool.h"
#include "stdlib.h"
#include "task.h"

// Small worker thread to handle power (PD + QC) related steps

void startPOWTask(void const *argument __unused) {

  // Init any other misc sensors
  postRToSInit();
  while (preStartChecksDone() == 0) {
    osDelay(3);
  }
  // You have to run this once we are willing to answer PD messages
  // Setting up too early can mean that we miss the ~20ms window to respond on some chargers
#ifdef POW_PD_STUSB4500
  STUSB4500::init();
#endif

  for (;;) {
#ifdef POW_PD_STUSB4500
    STUSB4500::check_negotiation();
#endif
    power_check();
    // Delay before next iteration
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
