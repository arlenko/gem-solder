
#include "OperatingModeUtilities.h"

void GUIDelay() {
  // Called in all UI looping tasks,
  // This limits the re-draw rate to the LCD and also lets the DMA run
  // As the gui task can very easily fill this bus with transactions
  vTaskDelay(5 * TICKS_10MS);
}