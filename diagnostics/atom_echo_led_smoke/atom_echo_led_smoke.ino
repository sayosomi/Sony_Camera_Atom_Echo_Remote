#include <Arduino.h>

namespace {

constexpr uint8_t kLedPin = 27;
constexpr uint8_t kBrightness = 32;
constexpr uint32_t kStepMs = 1000;

void setLed(uint8_t red, uint8_t green, uint8_t blue) {
  rgbLedWrite(kLedPin, red, green, blue);
}

}  // namespace

void setup() {
  delay(250);
}

void loop() {
  setLed(kBrightness, 0, 0);
  delay(kStepMs);

  setLed(0, kBrightness, 0);
  delay(kStepMs);

  setLed(0, 0, kBrightness);
  delay(kStepMs);

  setLed(kBrightness, 0, kBrightness);
  delay(kStepMs);

  setLed(0, 0, 0);
  delay(kStepMs);
}
