// BSP mapping functions

#include "BSP.h"
#include "BootLogo.h"
#include "I2C_Wrapper.hpp"
#include "OperatingModes.h"
#include "Pins.h"
#include "STUSB4500.hpp"
#include "Settings.h"
#include "Setup.h"
#include "TipProfile.hpp"
#include "TipThermoModel.h"
#include "USBPD.h"
#include "configuration.h"
#include "history.hpp"
#include "main.hpp"
#include <IRQ.h>

#ifdef WS2812_ENABLE
#include "WS2812.h"

WS2812<GPIOB_BASE, WS2812_Pin, 1> ws2812;
#endif

volatile uint16_t PWMSafetyTimer = 0;
volatile uint8_t  pendingPWM     = 0;

const uint16_t       powerPWM         = TIP_PWM_ARR;
static const uint8_t holdoffTicks     = 20; // delay of ~4 ms
static const uint8_t tempMeasureTicks = 20;

uint16_t totalPWM; // htimADC.Init.Period, the full PWM cycle

static bool fastPWM;
static bool infastPWM;

static volatile bool        currentSamplingActive   = false;
static volatile uint32_t    lastCurrentSamplingTick = 0;
static history<uint32_t, 2> rawCurrentSamplesFilter = {{0}, 0, 0};

extern OperatingMode currentOperatingMode;

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

uint16_t getStandSenseVoltagemV() {
  // The stand sense pin only reads correctly while the heater MOSFET is off: heater current
  // couples into the tip and from there into the stand sense line, reading
  // falsely high during conduction. Sample the pin voltage makes it more flexible
  // and trustworhy than HAL_GPIO_ReadPin reading.
  uint16_t adc = (hadc2.Instance->JDR3 + hadc2.Instance->JDR4) >> 1;
  return (uint16_t)(((uint32_t)adc * ADC_VDD_MV) / 4096);
}

// We may need to disable current sampling for some operating modes
bool currentSamplingAllowed(OperatingMode opMode) {
#if defined(POW_PD) || defined(POW_PD_STUSB4500)
  // For PD capable device we wait for PD negotiation or time out
  if (!STUSB4500::has_negotiated() && HAL_GetTick() < TICKS_SECOND * 2)
    return false;
#else
  if (HAL_GetTick() < TICKS_SECOND * 0.5)
    return false; // Startup delay to allow hardware to settle
#endif

  switch (opMode) {
  case OperatingMode::Hibernating:
  case OperatingMode::ThermalRunaway:
  case OperatingMode::CJCCalibration:
    return false;
  default:
    return true;
  }
}

static void switchToFastPWM(void) {
  // 22Hz
  infastPWM              = true;
  totalPWM               = powerPWM + tempMeasureTicks + holdoffTicks;
  htimADC.Instance->ARR  = totalPWM;
  htimADC.Instance->CCR1 = powerPWM + holdoffTicks;
  htimADC.Instance->PSC  = 1580;
}

/*
static void switchToSlowPWM(void) {
  // 5Hz
  infastPWM              = false;
  totalPWM               = powerPWM + tempMeasureTicks / 2 + holdoffTicks / 2;
  htimADC.Instance->ARR  = totalPWM;
  htimADC.Instance->CCR1 = powerPWM + holdoffTicks / 2;
  htimADC.Instance->PSC  = 2690 * 2;
}
*/

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

    uint32_t              now          = HAL_GetTick();
    uint8_t               thisCyclePWM = pendingPWM; // Use callback scoped variable to avoid pendingPWM overwrite
    static const uint32_t currentSamplingInterval = TICKS_SECOND / 2;
    bool                  shouldSampleCurrent =
        (lastCurrentSamplingTick == 0 || (now - lastCurrentSamplingTick) > currentSamplingInterval);

    // Force higher duty cycle if current sampling needed.
    // Once interrupt callback fires it will set duty cycle to pendingPWM
    if (shouldSampleCurrent && currentSamplingAllowed(currentOperatingMode)) {
      thisCyclePWM = pendingPWM >= TIP_MEASUREMENT_DUTY ? pendingPWM : TIP_MEASUREMENT_DUTY;
      // At 20khz duration of a single pulse at 100% duty is 50us. Actual current sampling duty is ~80%.
      // It takes ~4us for the ADC to complete conversion so we are safe to measure current at the
      // middle of a pulse or a bit past the middle
      uint8_t currentSamplingChannelPeriod = (thisCyclePWM * 6) / 10;
      __HAL_TIM_SET_COMPARE(&htimTip, TIM_CHANNEL_2, currentSamplingChannelPeriod);
      currentSamplingActive = true;
      // Enable interrupt trigger
      __HAL_TIM_CLEAR_FLAG(&htimTip, TIM_FLAG_CC2);
      __HAL_TIM_ENABLE_IT(&htimTip, TIM_IT_CC2);
    } else {
      __HAL_TIM_SET_COMPARE(&htimTip, TIM_CHANNEL_2, 0xFFFF); // Set unreachable count to skip ADC trigger
    }

    __HAL_TIM_SET_COMPARE(&htimADC, TIM_CHANNEL_4, powerPWM);
    if (thisCyclePWM && PWMSafetyTimer) {
      __HAL_TIM_SET_COMPARE(&htimTip, PWM_Out_CHANNEL, thisCyclePWM);
      HAL_TIM_PWM_Start(&htimTip, PWM_Out_CHANNEL);
    } else {
      HAL_TIM_PWM_Stop(&htimTip, PWM_Out_CHANNEL);
    }

    if (!infastPWM) {
      switchToFastPWM();
    }

  } else if (htim->Instance == TIM1) {
    // STM uses this for internal functions as a counter for timeouts
    HAL_IncTick();
  }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4) {
    // This was a when the PWM for the output has timed out
    HAL_TIM_PWM_Stop(&htimTip, PWM_Out_CHANNEL);
  } else if (htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
    // This is when a single PWM pulse finished
    if (currentSamplingActive) {
      lastCurrentSamplingTick = HAL_GetTick();
      currentSamplingActive   = false;
      __HAL_TIM_DISABLE_IT(&htimTip, TIM_IT_CC2);
      __HAL_TIM_CLEAR_FLAG(&htimTip, TIM_FLAG_CC2);
      __HAL_TIM_SET_COMPARE(&htimTip, TIM_CHANNEL_2, 0xFFFF); // Stop the ADC trigger
      __HAL_TIM_SET_COMPARE(&htimTip, PWM_Out_CHANNEL, pendingPWM);
      if (pendingPWM == 0) {
        // Stop PWM output if the iron is idling
        HAL_TIM_PWM_Stop(&htimTip, PWM_Out_CHANNEL);
      }
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

void BSPInit(void) {
  switchToFastPWM();
#ifdef WS2812_ENABLE
  ws2812.init();
#endif
}

void reboot() { NVIC_SystemReset(); }

void delay_ms(uint16_t count) { HAL_Delay(count); }

uint8_t       lastTipResistance        = 0; // default to unknown
const uint8_t numTipResistanceReadings = 3;
uint32_t      tipResistanceReadings[3] = {0, 0, 0};
uint8_t       tipResistanceReadingSlot = 0;

bool isTipDisconnected() {
  // If startup behaviour setting is soldering screen
  // we suppose that tip is connected so it wouldn't quit the soldering screen on power up
  // while connection state is unknown
  if (lastCurrentSamplingTick == 0)
    return false;
  return getCurrentMilliamps() <= TIP_DISCONNECT_CURRENT_MA;
}

void setStatusLED(const enum StatusLED state) {
#ifdef WS2812_ENABLE
  static enum StatusLED lastState = LED_UNKNOWN;

  if (lastState != state || state == LED_HEATING || state == LED_COOLING_STILL_HOT) {
    switch (state) {
    default:
    case LED_UNKNOWN:
    case LED_OFF:
      ws2812.led_set_color(0, 0, 0, 0);
      break;
    case LED_STANDBY:
      ws2812.led_set_color(0, 0, 0x9E, 0); // green
      break;
    case LED_HEATING: {
      static const uint32_t half_period = 960; // ms for dim->saturated (tune speed here)
      const uint32_t        t           = HAL_GetTick() % (half_period * 2);
      const uint32_t        tri         = (t < half_period) ? t : (half_period * 2 - t);
      const uint8_t         red         = (uint8_t)(64 + (tri * (255 - 64)) / half_period);
      ws2812.led_set_color(0, red, 0, 0);
    } break;
    case LED_HOT:
      ws2812.led_set_color(0, 0xFF, 0, 0); // red
      break;
    case LED_COOLING_STILL_HOT: {
      static const uint32_t half_period = 1500; // ms for dim->saturated (tune speed here)
      const uint32_t        t           = HAL_GetTick() % (half_period * 2);
      const uint32_t        tri         = (t < half_period) ? t : (half_period * 2 - t);
      const uint8_t         red         = (uint8_t)(64 + (tri * (194 - 64)) / half_period);
      const uint8_t         green       = (uint8_t)(64 + (tri * (194 - 64)) / half_period);
      ws2812.led_set_color(0, red, green, 0);
    } break;
    case LED_SLEEPING:
      ws2812.led_set_color(0, 0x40, 0x00, 0x80); // dark violet #400080
      break;
    }
    ws2812.led_update();
    lastState = state;
  }
#endif
}

void setBuzzer(bool on) {}

uint8_t preStartChecks() { return 1; }

uint64_t getDeviceID() {
  //
  return HAL_GetUIDw0() | ((uint64_t)HAL_GetUIDw1() << 32);
}

uint8_t preStartChecksDone() {
  // For a correct power estimation current sampling must be completed before start
  return (lastCurrentSamplingTick == 0 || currentSamplingActive || isTipShorted()) ? 0 : 1;
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

uint16_t getTipThermalMass() { return TIP_C245.thermalMass; }
uint16_t getTipInertia() { return TIP_C245.inertia; }
uint8_t  getTipPowerRating() { return TIP_C245.powerRating; }

void showBootLogo(void) { BootLogo::handleShowingLogo((uint8_t *)FLASH_LOGOADDR); }
