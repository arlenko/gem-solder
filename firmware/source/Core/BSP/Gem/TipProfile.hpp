#pragma once
#include <stdint.h>

enum class TipType : uint8_t { C245, C210, MAX };

struct TipProfile {
  TipType type;
  uint8_t thermalMass;
  uint8_t inertia;
  uint8_t powerRating;
};

/**
 * We use a large inertia value to smooth out the drive to the tip since its stupidly sensitive
 *                                                                                  Ralim. 2023
 * Higher inertia helps reduce overshoot and maintain a more stable temperature at the setpoint.
 * However, it also increases the temperature drop under soldering load
 */
inline const TipProfile TIP_C245 = {.type = TipType::C245, .thermalMass = 40, .inertia = 124, .powerRating = 140};
inline const TipProfile TIP_C210 = {.type = TipType::C210, .thermalMass = 10, .inertia = 128, .powerRating = 60};