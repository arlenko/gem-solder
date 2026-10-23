#ifndef OPERATING_MODE_UTILITIES_H_
#define OPERATING_MODE_UTILITIES_H_
#include "Buttons.hpp"
#include "OLED.hpp"
#include "Settings.h"
#include <stdbool.h>

void     GUIDelay();                   //
bool     checkForUnderVoltage(void);   //
bool     shouldBeSleeping();           //
bool     shouldShutdown(void);         //
void     clearTapToSleep(void);        //
void     printVoltage(void);           //
uint16_t min(uint16_t a, uint16_t b);  //
#endif
