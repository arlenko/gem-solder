// BSP mapping functions

#include "BSP.h"
#include "BootLogo.h"
#include "I2C_Wrapper.hpp"
#include "OperatingModes.h"
#include "Pins.h"
#include "Settings.h"
#include "Setup.h"
#include "TipThermoModel.h"
#include "USBPD.h"
#include "configuration.h"
#include "history.hpp"
#include "main.hpp"
#include <IRQ.h>

volatile uint16_t PWMSafetyTimer = 0;
volatile uint8_t  pendingPWM     = 0;

const uint16_t       powerPWM         = 255;
static const uint8_t holdoffTicks     = 14; // delay of 8 ms
static const uint8_t tempMeasureTicks = 14;

uint16_t totalPWM; // htimADC.Init.Period, the full PWM cycle

static bool fastPWM;
static bool infastPWM;

static volatile bool     currentSamplingActive   = false;
static volatile uint32_t lastCurrentSamplingTick = 0;

extern OperatingMode currentOperatingMode;

static history<uint32_t, 6> rawCurrentSamplesFilter = {{0}, 0, 0};

void resetWatchdog() { HAL_IWDG_Refresh(&hiwdg); }
#ifdef TEMP_NTC
// Lookup table for the NTC
// Stored as ADCReading,Temp in degC
static const uint16_t NTCHandleLookup[] = {
    // ADC Reading , Temp in C
    29189, 0,  //
    28832, 2,  //
    28450, 4,  //
    28042, 6,  //
    27607, 8,  //
    27146, 10, //
    26660, 12, //
    26147, 14, //
    25610, 16, //
    25049, 18, //
    24465, 20, //
    23859, 22, //
    23234, 24, //
    22591, 26, //
    21933, 28, //
    21261, 30, //
    20579, 32, //
    19888, 34, //
    19192, 36, //
    18493, 38, //
    17793, 40, //
    17096, 42, //
    16404, 44, //
    16061, 45, //
};
#endif

int16_t getHandleTemperature(uint8_t sample) {
  int32_t result = getADCHandleTemp(sample);
#ifdef TEMP_NTC
  // TS80P uses 100k NTC resistors instead
  // NTCG104EF104FT1X from TDK
  // For now not doing interpolation
  for (uint32_t i = 0; i < (sizeof(NTCHandleLookup) / (2 * sizeof(uint16_t))); i++) {
    if (result > NTCHandleLookup[(i * 2) + 0]) {
      return NTCHandleLookup[(i * 2) + 1] * 10;
    }
  }
  return 45 * 10;
#endif
#ifdef TEMP_TMP36
  // We return the current handle temperature in X10 C
  // TMP36 in handle, 0.5V offset and then 10mV per deg C (0.75V @ 25C for
  // example) STM32 = 4096 count @ 3.3V input -> But We oversample by 32/(2^2) =
  // 8 times oversampling Therefore 32768 is the 3.3V input, so 0.1007080078125
  // mV per count So we need to subtract an offset of 0.5V to center on 0C
  // (4964.8 counts)
  //
  result -= 4965; // remove 0.5V offset
  // 10mV per C
  // 99.29 counts per Deg C above 0C. Tends to read a tad over across all of my sample units
  result *= 100;
  result /= 994;
  return result;
#endif
  return 0;
}

uint16_t getInputVoltageX10(uint16_t divisor, uint8_t sample) {
  // ADC maximum is 32767 == 3.3V at input == 28.05V at VIN
  // Therefore we can divide down from there
  // Multiplying ADC max by 4 for additional calibration options,
  // ideal term is 467
  uint32_t res = getADCVin(sample);
  res *= 4;
  res /= divisor;
  return res;
}

uint32_t getCurrentMilliamps() {
  uint32_t adc      = rawCurrentSamplesFilter.average();
  uint32_t v_adc_mV = ((uint32_t)adc * ADC_VDD_MV) / 4096;
  return (v_adc_mV * 1000) / (CURRENT_SENSE_SHUNT_RESISTANCE_mOhms * OP_AMP_CURRENT_SENSE_GAIN_STAGE);
}

uint32_t getCurrentSamplingInterval(OperatingMode opMode) {
  if (opMode == OperatingMode::Soldering && pendingPWM >= TIP_MEASUREMENT_DUTY) {
    // In soldering mode when the tip is actively heating do more frequent measurement
    return TICKS_100MS * 5;
  }
  return TICKS_SECOND;
}

uint8_t getCurrentSamplingDuty(OperatingMode opMode) {
  if (opMode == OperatingMode::Soldering || opMode == OperatingMode::DebugMenuReadout) {
    return TIP_MEASUREMENT_DUTY; // More precise measurement
  }
  return 20; // 25/255 is ~8% duty. Enough for sensing if the tip is connected
}

// We may need to disable current sampling for some operating modes
bool currentSamplingAllowed(OperatingMode opMode) {
  switch (opMode) {
  case OperatingMode::Sleeping:
  case OperatingMode::Hibernating:
  case OperatingMode::ThermalRunaway:
    return false;
  default:
    return true;
  }
}

static void switchToFastPWM(void) {
  // 10Hz
  infastPWM              = true;
  totalPWM               = powerPWM + tempMeasureTicks + holdoffTicks;
  htimADC.Instance->ARR  = totalPWM;
  htimADC.Instance->CCR1 = powerPWM + holdoffTicks;
  htimADC.Instance->PSC  = 2690;
}

static void switchToSlowPWM(void) {
  // 5Hz
  infastPWM              = false;
  totalPWM               = powerPWM + tempMeasureTicks / 2 + holdoffTicks / 2;
  htimADC.Instance->ARR  = totalPWM;
  htimADC.Instance->CCR1 = powerPWM + holdoffTicks / 2;
  htimADC.Instance->PSC  = 2690 * 2;
}

void setTipPWM(const uint8_t pulse, const bool shouldUseFastModePWM) {
  PWMSafetyTimer = 20; // This is decremented in the handler for PWM so that the tip pwm is
                       // disabled if the PID task is not scheduled often enough.
  fastPWM    = shouldUseFastModePWM;
  pendingPWM = pulse;
}
// These are called by the HAL after the corresponding events from the system
// timers.

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  // Period has elapsed
  if (htim->Instance == ADC_CONTROL_TIMER) {
    // we want to turn on the output again
    PWMSafetyTimer--;
    // We decrement this safety value so that lockups in the
    // scheduler will not cause the PWM to become locked in an
    // active driving state.
    // While we could assume this could never happen, its a small price for
    // increased safety

    uint32_t now                     = HAL_GetTick();
    uint32_t currentSamplingInterval = getCurrentSamplingInterval(currentOperatingMode);

    if (currentSamplingAllowed(currentOperatingMode) &&
        (lastCurrentSamplingTick == 0 || (now - lastCurrentSamplingTick) > currentSamplingInterval)) {
      uint8_t neededPWM = getCurrentSamplingDuty(currentOperatingMode);
      pendingPWM        = pendingPWM >= neededPWM ? pendingPWM : neededPWM;
      __HAL_TIM_SET_COMPARE(&htimTip, TIM_CHANNEL_2, pendingPWM / 2);
      currentSamplingActive = true;
    } else {
      __HAL_TIM_SET_COMPARE(&htimTip, TIM_CHANNEL_2, 0xFFFF);
    }

    htimADC.Instance->CCR4 = powerPWM;
    if (pendingPWM && PWMSafetyTimer) {
      __HAL_TIM_SET_COMPARE(&htimTip, PWM_Out_CHANNEL, pendingPWM);
      HAL_TIM_PWM_Start(&htimTip, PWM_Out_CHANNEL);
    } else {
      HAL_TIM_PWM_Stop(&htimTip, PWM_Out_CHANNEL);
    }

    if (fastPWM != infastPWM) {
      if (fastPWM) {
        switchToFastPWM();
      } else {
        switchToSlowPWM();
      }
    }

  } else if (htim->Instance == TIM1) {
    // STM uses this for internal functions as a counter for timeouts
    HAL_IncTick();
  }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
  // This was a when the PWM for the output has timed out
  if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4) {
    HAL_TIM_PWM_Stop(&htimTip, PWM_Out_CHANNEL);
    if (currentSamplingActive) {
      lastCurrentSamplingTick = HAL_GetTick();
      currentSamplingActive   = false;
    }
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
  if (hadc == &hadc2) {
    // ADC2 regular conversion (CURRENT_SENSE) complete
    rawCurrentSamplesFilter.update(HAL_ADC_GetValue(hadc));
  }
}

void unstick_I2C() {
#ifndef I2C_SOFT_BUS_1
  GPIO_InitTypeDef GPIO_InitStruct;
  int              timeout     = 100;
  int              timeout_cnt = 0;

  // 1. Clear PE bit.
  hi2c1.Instance->CR1 &= ~(0x0001);
  /**I2C1 GPIO Configuration
   PB6     ------> I2C1_SCL
   PB7     ------> I2C1_SDA
   */
  //  2. Configure the SCL and SDA I/Os as General Purpose Output Open-Drain, High level (Write 1 to GPIOx_ODR).
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  GPIO_InitStruct.Pin = SCL_Pin;
  HAL_GPIO_Init(SCL_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = SDA_Pin;
  HAL_GPIO_Init(SDA_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(SDA_GPIO_Port, SDA_Pin, GPIO_PIN_SET);

  while (GPIO_PIN_SET != HAL_GPIO_ReadPin(SDA_GPIO_Port, SDA_Pin)) {
    // Move clock to release I2C
    HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_RESET);
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_SET);

    timeout_cnt++;
    if (timeout_cnt > timeout) {
      return;
    }
  }

  // 12. Configure the SCL and SDA I/Os as Alternate function Open-Drain.
  GPIO_InitStruct.Mode  = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

  GPIO_InitStruct.Pin = SCL_Pin;
  HAL_GPIO_Init(SCL_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = SDA_Pin;
  HAL_GPIO_Init(SDA_GPIO_Port, &GPIO_InitStruct);

  HAL_GPIO_WritePin(SCL_GPIO_Port, SCL_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SDA_GPIO_Port, SDA_Pin, GPIO_PIN_SET);

  // 13. Set SWRST bit in I2Cx_CR1 register.
  hi2c1.Instance->CR1 |= 0x8000;

  asm("nop");

  // 14. Clear SWRST bit in I2Cx_CR1 register.
  hi2c1.Instance->CR1 &= ~0x8000;

  asm("nop");

  // 15. Enable the I2C peripheral by setting the PE bit in I2Cx_CR1 register
  hi2c1.Instance->CR1 |= 0x0001;

  // Call initialization function.
  HAL_I2C_Init(&hi2c1);
#endif
}

uint8_t getButtonA() { return HAL_GPIO_ReadPin(KEY_A_GPIO_Port, KEY_A_Pin) == GPIO_PIN_SET ? 1 : 0; }
uint8_t getButtonB() { return HAL_GPIO_ReadPin(KEY_B_GPIO_Port, KEY_B_Pin) == GPIO_PIN_SET ? 1 : 0; }

void BSPInit(void) { switchToFastPWM(); }

void reboot() { NVIC_SystemReset(); }

void delay_ms(uint16_t count) { HAL_Delay(count); }

uint8_t       lastTipResistance        = 0; // default to unknown
const uint8_t numTipResistanceReadings = 3;
uint32_t      tipResistanceReadings[3] = {0, 0, 0};
uint8_t       tipResistanceReadingSlot = 0;

bool isTipDisconnected() { return getCurrentMilliamps() <= TIP_DISCONNECT_CURRENT_MA; }

void setStatusLED(const enum StatusLED state) {}
void setBuzzer(bool on) {}

uint8_t preStartChecks() { return 1; }

uint64_t getDeviceID() {
  //
  return HAL_GetUIDw0() | ((uint64_t)HAL_GetUIDw1() << 32);
}

uint8_t preStartChecksDone() {
#ifdef TIP_RESISTANCE_SENSE_Pin
  return (lastTipResistance == 0 || tipResistanceReadingSlot < numTipResistanceReadings || tipMeasurementOccuring ||
          tipShorted)
             ? 0
             : 1;
#else
  return 1;
#endif
}

uint8_t getTipResistanceX10() {
  // Aftermarket tips may have different resistance
  // so would be better to rely on the actual resistance masurement
  uint32_t i = getCurrentMilliamps();
  uint32_t v = getInputVoltageX10(getSettingValue(SettingsOptions::VoltageDiv), 0); // 100 = 10v
  // Disregard possible division by 0 fallback for now
  return v * 1000 / i;
}

bool isTipShorted() { return getCurrentMilliamps() >= TIP_SHORT_CURRENT_MA; }

uint16_t getTipThermalMass() {
  uint8_t r = getTipResistanceX10();
  if (r >= 80) {
    return TIP_THERMAL_MASS; // high-resistance tip
  }
  return 45; // low-resistance tip
}
uint16_t getTipInertia() {
  uint8_t r = getTipResistanceX10();
  if (r >= 80) {
    return TIP_THERMAL_MASS;
  }
  return 10;
}

void showBootLogo(void) { BootLogo::handleShowingLogo((uint8_t *)FLASH_LOGOADDR); }
