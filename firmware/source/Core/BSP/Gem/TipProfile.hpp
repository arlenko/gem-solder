#pragma once
#include <stdint.h>

enum class TipType : uint8_t { C245, C210, MAX };

struct TipProfile {
  TipType type;
  uint8_t thermalMass;
  uint8_t inertia;
};

inline const TipProfile TIP_C245 = {.type = TipType::C245, .thermalMass = 40, .inertia = 15};
inline const TipProfile TIP_C210 = {.type = TipType::C210, .thermalMass = 30, .inertia = 10};