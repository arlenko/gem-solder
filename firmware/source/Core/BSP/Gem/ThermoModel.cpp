/*
 * ThermoModel.cpp
 *
 *  Created on: 1 May 2021
 *      Author: Ralim
 */
#include "TipThermoModel.h"
#include "Utils.hpp"
#include "configuration.h"

#ifdef TEMP_uV_LOOKUP_C245
// It appears that 27.3 uV/C with K-type linear characteristics
// has the best match with the actual tip temperature.
// Needs further testing with different tips
const int32_t uVtoDegC[] = {
    //
    //
    0,     0,   //
    262,   10,  //
    528,   20,  //
    795,   30,  //
    1065,  40,  //
    1337,  50,  //
    1611,  60,  //
    1884,  70,  //
    2160,  80,  //
    2434,  90,  //
    2708,  100, //
    2981,  110, //
    3252,  120, //
    3522,  130, //
    3791,  140, //
    4058,  150, //
    4324,  160, //
    4588,  170, //
    4852,  180, //
    5116,  190, //
    5380,  200, //
    5645,  210, //
    5910,  220, //
    6176,  230, //
    6443,  240, //
    6712,  250, //
    6982,  260, //
    7253,  270, //
    7525,  280, //
    7798,  290, //
    8071,  300, //
    8346,  310, //
    8622,  320, //
    8897,  330, //
    9174,  340, //
    9450,  350, //
    9728,  360, //
    10005, 370, //
    10283, 380, //
    10562, 390, //
    10841, 400, //
    11120, 410, //
    11400, 420, //
    11680, 430, //
    11960, 440, //
    12241, 450, //
    12522, 460, //
    12803, 470, //
    13085, 480, //
    13367, 490, //
    13650, 500, //
};
#endif

const int uVtoDegCItems = sizeof(uVtoDegC) / (2 * sizeof(uVtoDegC[0]));

TemperatureType_t TipThermoModel::convertuVToDegC(uint32_t tipuVDelta) {
  return Utils::InterpolateLookupTable(uVtoDegC, uVtoDegCItems, tipuVDelta);
}
