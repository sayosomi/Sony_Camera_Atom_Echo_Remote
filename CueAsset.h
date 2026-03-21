#pragma once

#include <Arduino.h>

struct CueAsset {
  const uint8_t* wavData;
  size_t wavLength;
};
