#ifndef CONFIGURATION_H_
#define CONFIGURATION_H_
#include <stdint.h>
/**
 * Configuration.h
 * Define here your default settings
 *
 */

//===========================================================================
//============================= Default Settings ============================
//===========================================================================
/**
 * Default soldering temp is 320.0 C
 * Temperature the iron sleeps at - default 150.0 C
 */

#define SLEEP_TEMP         150 // Default sleep temperature
#define BOOST_TEMP         420 // Default boost temp.
#define BOOST_MODE_ENABLED 1   // 0: Disable 1: Enable

/**
 * Blink the temperature on the cooling screen when its > 50C
 */
#define COOLING_TEMP_BLINK 0 // 0: Disable 1: Enable

/**
 * How many seconds/minutes we wait until going to sleep/shutdown.
 * Values -> SLEEP_TIME * 10; i.e. 5*10 = 50 Seconds!
 */
#define SLEEP_TIME    5  // x10 Seconds
#define SHUTDOWN_TIME 10 // Minutes

/**
 * Auto start off for safety.
 * Possible values are:
 *  0 - none
 *  1 - Soldering Temperature
 *  2 - Sleep Temperature
 *  3 - Sleep Off Temperature
 */
#define AUTO_START_MODE 0 // Default to none

/**
 * Locking Mode
 * When in soldering mode a long press on both keys toggle the lock of the buttons
 * Possible values are:
 *  0 - Desactivated
 *  1 - Lock except boost
 *  2 - Full lock
 */
#define LOCKING_MODE 0 // Default to desactivated for safety

/**
 * OLED Orientation
 *
 */
#define ORIENTATION_MODE     0 // 0: Right 1:Left 2:Automatic - Default Automatic
#define MAX_ORIENTATION_MODE 1 // Up to auto
// 0:Default 1:Reverse - Reverse the plus and minus button assigment for temperature change
#define REVERSE_BUTTON_TEMP_CHANGE 0

/**
 * OLED Brightness
 *
 */
//   #define MIN_BRIGHTNESS     1   // Min OLED brightness selectable
//   #define MAX_BRIGHTNESS     101 // Max OLED brightness selectable
#define MIN_BRIGHTNESS     0   // Min OLED brightness selectable
#define MAX_BRIGHTNESS     100 // Max OLED brightness selectable
#define BRIGHTNESS_STEP    25  // OLED brightness increment
#define DEFAULT_BRIGHTNESS 25  // default OLED brightness

/**
 * Temp change settings
 */
#define TEMP_CHANGE_SHORT_STEP     1  // Default temp change short step +1
#define TEMP_CHANGE_LONG_STEP      10 // Default temp change long step +10
#define TEMP_CHANGE_SHORT_STEP_MAX 50 // Temp change short step MAX value
#define TEMP_CHANGE_LONG_STEP_MAX  90 // Temp change long step MAX value

/* Power pulse for keeping power banks awake*/
#define POWER_PULSE_INCREMENT    1
#define POWER_PULSE_MAX          100 // x10 max watts
#define POWER_PULSE_WAIT_MAX     9   // 9*2.5s = 22.5 seconds
#define POWER_PULSE_DURATION_MAX 9   // 9*250ms = 2.25 seconds

#define POWER_PULSE_DEFAULT          0
#define POWER_PULSE_WAIT_DEFAULT     4 // Default rate of the power pulse: 4*2500 = 10000 ms = 10 s
#define POWER_PULSE_DURATION_DEFAULT 1 // Default duration of the power pulse: 1*250 = 250 ms

/**
 * OLED Orientation Sensitivity on Automatic mode!
 * Motion Sensitivity <0=Off 1=Least Sensitive 9=Most Sensitive>
 */
#define SENSITIVITY 0 // Default 7

/**
 * Detailed soldering screen
 * Detailed idle screen (off for first time users)
 */
#define DETAILED_SOLDERING 0 // 0: Disable 1: Enable - Default 0
#define DETAILED_IDLE      0 // 0: Disable 1: Enable - Default 0

#define THERMAL_RUNAWAY_TIME_SEC 20
#define THERMAL_RUNAWAY_TEMP_C   3

#define CUT_OUT_SETTING          0  // default to no cut-off voltage
#define RECOM_VOL_CELL           33 // Minimum voltage per cell (Recommended 3.3V (33))
#define TEMPERATURE_INF          0  // default to 0
#define DESCRIPTION_SCROLL_SPEED 0  // 0: Slow 1: Fast - default to slow
#define ANIMATION_LOOP           1  // 0: off 1: on
#define ANIMATION_SPEED          settingOffSpeed_t::MEDIUM

#define ADC_MAX_READING (4096 * 8) // Maximum reading of the adc
#define ADC_VDD_MV      3300       // ADC max reading millivolts

#define POW_PD_EXT 0

// Deriving the Voltage div:
// Vin_max = (3.3*(r1+r2))/(r2)
// vdiv = (32768*4)/(vin_max*10)

#if defined(MODEL_Gem) > 1
#error "Multiple models defined!"
#elif defined(MODEL_Gem) == 0
#error "No model defined!"
#endif
#define NEEDS_VBUS_PROBE 0

#define MIN_CALIBRATION_OFFSET 100 // Min value for calibration
#define SOLDERING_TEMP         320 // Default soldering temp is 320.0 °C
#define PID_TIM_HZ             (8) // Tick rate of the PID loop
#define MAX_TEMP_C             450 // Max soldering temp selectable °C
#define MAX_TEMP_F             850 // Max soldering temp selectable °F
#define MIN_TEMP_C             10  // Min soldering temp selectable °C
#define MIN_TEMP_F             50  // Min soldering temp selectable °F
#define MIN_BOOST_TEMP_C       250 // The min settable temp for boost mode °C
#define MIN_BOOST_TEMP_F       480 // The min settable temp for boost mode °F

#define I2C_SOFT_BUS_1 1

#ifdef MODEL_Gem
#define NO_ACCEL    1
#define STAND_SENSE 1

#define OP_AMP_CURRENT_SENSE_Rf              240 * 1000
#define OP_AMP_CURRENT_SENSE_Rin             5.1 * 1000
#define OP_AMP_CURRENT_SENSE_GAIN_STAGE      ((OP_AMP_CURRENT_SENSE_Rf) / (OP_AMP_CURRENT_SENSE_Rin))
#define CURRENT_SENSE_SHUNT_RESISTANCE_mOhms 5
#define TIP_DISCONNECT_CURRENT_MA            10    // threshold below which tip is considered disconnected
#define TIP_SHORT_CURRENT_MA                 12000 // threshold above which tip is considered shorted
#define TIP_MEASUREMENT_DUTY                 40    // 40/255 is ~15% PWM duty

#define VOLTAGE_DIV           282 // Resistive divider from schematic
#define CALIBRATION_OFFSET    900 // 900 - Default adc offset in uV
#define PID_POWER_LIMIT       100 // Sets the max pwm power limit
#define POWER_LIMIT           0   // 0 watts default limit
#define MAX_POWER_LIMIT       100
#define POWER_LIMIT_STEPS     5
#define OP_AMP_Rf_Gem         390 * 1000 // 390  Kilo-ohms -> From schematic
#define OP_AMP_Rin_Gem        1500       // 1.5 Kilo-ohms -> From schematic
#define OP_AMP_GAIN_STAGE_Gem ((OP_AMP_Rf_Gem) / (OP_AMP_Rin_Gem))
#define OP_AMP_GAIN_STAGE     OP_AMP_GAIN_STAGE_Gem
#define TEMP_uV_LOOKUP_C245
#define HARDWARE_MAX_WATTAGE_X10 1000
#define TIP_THERMAL_MASS         65 // X10 watts to raise 1 deg C in 1 second
#define TIP_RESISTANCE           30 // x10 ohms

#define TIP_HAS_DIRECT_PWM 1
#define POW_DC             1
// #define POW_PD               1
// #define USB_PD_EPR_WATTAGE   140 /* EPR Supported */
// #define I2C_SOFT_BUS_2       1
#define OLED_I2CBB1 1
// #define USB_PD_I2CBB2        1
#define USB_PD_VMAX 20
#define OLED_128x32 1
// #define OLED_FLIP            1
// #define HAS_SPLIT_POWER_PATH 1
#define TEMP_TMP36 1 // Gem uses MCP9700 which has the same characteristics as TMP36
// #define ACCEL_I2CBB1         1
// #define POW_EPR              1
// #define TIP_TYPE_SUPPORT     1 // Support for tips of different types, i.e. resistance
// #define AUTO_TIP_SELECTION   1 // Can auto-select the tip
// #define TIPTYPE_T12          1 // Can manually pick a T12 tip
// #define HAS_POWER_DEBUG_MENU
// #define DEBUG_POWER_MENU_BUTTON_B

#endif

#define FLASH_LOGOADDR      (0x08000000 + (62 * 1024))
#define SETTINGS_START_PAGE (0x08000000 + (63 * 1024))

#endif /* CONFIGURATION_H_ */
