#ifndef __MAIN_H
#define __MAIN_H
#include "OLED.hpp"
#include "Setup.h"
#include "Types.h"
#include <stdint.h>
extern volatile TemperatureType_t currentTempTargetDegC;
extern bool                       settingsWereReset;
extern bool                       usb_pd_available;
#ifdef __cplusplus
extern "C" {
#endif

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);

// Threads
void                startGUITask(void const *argument);
void                startPIDTask(void const *argument);
void                startPOWTask(void const *argument);
extern TaskHandle_t pidTaskNotification;
extern int32_t      powerSupplyWattageLimit;
#ifdef __cplusplus
}
#endif

bool thermalRunawayTripped(void);

#endif /* __MAIN_H */
