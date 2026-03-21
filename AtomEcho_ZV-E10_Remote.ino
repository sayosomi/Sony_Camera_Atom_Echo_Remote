#include <M5Unified.h>
#include <esp_system.h>

#include "AudioAssets.h"
#include "ShutterVariants.h"
#include "SonyBleRemote.h"

namespace {

SonyBleRemote remote;
m5::Button_Class buttonA;

struct CueAsset {
  const uint8_t* wavData;
  size_t wavLength;
};

enum class LedState : uint8_t {
  Unknown,
  Unpaired,
  PairedDisconnected,
  Connected,
};

enum class PendingAction : uint8_t {
  None,
  ShutterAfterCue,
};

PendingAction pendingAction = PendingAction::None;
LedState currentLedState = LedState::Unknown;
bool resetLatch = false;
bool pairedCuePlayedThisBoot = false;
bool previousReadyState = false;
SonyBleRemote::State previousRemoteState = SonyBleRemote::State::Booting;
bool previousConnectedState = false;
bool previousStoredPeerState = false;
int8_t lastShutterCueIndex = -1;

constexpr CueAsset kShutterCueOptions[] = {
  {kShutterWav, kShutterWavLen},
  {kShutter2Wav, kShutter2WavLen},
  {kShutter3Wav, kShutter3WavLen},
};
constexpr size_t kShutterCueCount =
    sizeof(kShutterCueOptions) / sizeof(kShutterCueOptions[0]);
constexpr uint8_t kLedPin = 27;
constexpr uint8_t kLedBrightness = 48;
constexpr bool kEnableSerialDebug = true;

bool isReadyState(SonyBleRemote::State state) {
  switch (state) {
    case SonyBleRemote::State::Ready:
    case SonyBleRemote::State::Focusing:
    case SonyBleRemote::State::Shooting:
      return true;
    case SonyBleRemote::State::Booting:
    case SonyBleRemote::State::ScanningPair:
    case SonyBleRemote::State::ScanningReconnect:
    case SonyBleRemote::State::Connecting:
    case SonyBleRemote::State::Error:
      return false;
  }

  return false;
}

const __FlashStringHelper* stateLabel(SonyBleRemote::State state) {
  switch (state) {
    case SonyBleRemote::State::Booting: return F("Booting");
    case SonyBleRemote::State::ScanningPair: return F("ScanningPair");
    case SonyBleRemote::State::ScanningReconnect: return F("ScanningReconnect");
    case SonyBleRemote::State::Connecting: return F("Connecting");
    case SonyBleRemote::State::Ready: return F("Ready");
    case SonyBleRemote::State::Focusing: return F("Focusing");
    case SonyBleRemote::State::Shooting: return F("Shooting");
    case SonyBleRemote::State::Error: return F("Error");
  }

  return F("Unknown");
}

void logSnapshotChange(const SonyBleRemote::Snapshot& shot, bool ready) {
  if (!kEnableSerialDebug) {
    return;
  }

  if (shot.state == previousRemoteState && shot.connected == previousConnectedState &&
      shot.hasStoredPeer == previousStoredPeerState && ready == previousReadyState) {
    return;
  }

  Serial.print(F("[remote] state="));
  Serial.print(stateLabel(shot.state));
  Serial.print(F(" connected="));
  Serial.print(shot.connected ? F("true") : F("false"));
  Serial.print(F(" storedPeer="));
  Serial.print(shot.hasStoredPeer ? F("true") : F("false"));
  Serial.print(F(" ready="));
  Serial.println(ready ? F("true") : F("false"));
}

LedState ledStateFromSnapshot(const SonyBleRemote::Snapshot& shot) {
  if (isReadyState(shot.state)) {
    return LedState::Connected;
  }

  if (shot.hasStoredPeer) {
    return LedState::PairedDisconnected;
  }

  return LedState::Unpaired;
}

void applyLedState(LedState nextState) {
  if (nextState == currentLedState) {
    return;
  }

  currentLedState = nextState;

  if (kEnableSerialDebug) {
    Serial.print(F("[led] "));
    switch (nextState) {
      case LedState::Unpaired:
        Serial.println(F("Unpaired rgb(48,0,48)"));
        break;
      case LedState::PairedDisconnected:
        Serial.println(F("PairedDisconnected rgb(48,0,0)"));
        break;
      case LedState::Connected:
        Serial.println(F("Connected rgb(0,48,0)"));
        break;
      case LedState::Unknown:
        Serial.println(F("Unknown rgb(0,0,0)"));
        break;
    }
  }

  switch (nextState) {
    case LedState::Unpaired:
      rgbLedWrite(kLedPin, kLedBrightness, 0, kLedBrightness);
      return;
    case LedState::PairedDisconnected:
      rgbLedWrite(kLedPin, kLedBrightness, 0, 0);
      return;
    case LedState::Connected:
      rgbLedWrite(kLedPin, 0, kLedBrightness, 0);
      return;
    case LedState::Unknown:
      rgbLedWrite(kLedPin, 0, 0, 0);
      return;
  }
}

void updateLedState() {
  applyLedState(ledStateFromSnapshot(remote.snapshot()));
}

bool speakerBusy() {
  return M5.Speaker.isPlaying();
}

bool playCue(const uint8_t* wavData, size_t wavLength, bool stopCurrentSound = true) {
  if (!M5.Speaker.isEnabled()) {
    return false;
  }

  return M5.Speaker.playWav(wavData, wavLength, 1, 0, stopCurrentSound);
}

void stopCue() {
  pendingAction = PendingAction::None;
  if (M5.Speaker.isEnabled()) {
    M5.Speaker.stop();
  }
}

CueAsset selectShutterCue() {
  if (kShutterCueCount == 1) {
    lastShutterCueIndex = 0;
    return kShutterCueOptions[0];
  }

  const uint32_t draw = esp_random() % (kShutterCueCount - 1);
  size_t selectedIndex = draw;

  if (lastShutterCueIndex >= 0 &&
      selectedIndex >= static_cast<size_t>(lastShutterCueIndex)) {
    ++selectedIndex;
  }

  lastShutterCueIndex = static_cast<int8_t>(selectedIndex);
  return kShutterCueOptions[selectedIndex];
}

void triggerPendingActionIfReady() {
  if (pendingAction == PendingAction::None || speakerBusy()) {
    return;
  }

  const PendingAction action = pendingAction;
  pendingAction = PendingAction::None;

  if (action == PendingAction::ShutterAfterCue) {
    remote.triggerShutter();
  }
}

void updatePairingCue() {
  if (pairedCuePlayedThisBoot) {
    return;
  }

  const SonyBleRemote::Snapshot shot = remote.snapshot();
  const bool ready = isReadyState(shot.state);
  if (!ready || previousReadyState) {
    return;
  }

  pairedCuePlayedThisBoot = true;
  playCue(kPairedWav, kPairedWavLen, true);
}

void updateConnectionTracking() {
  const SonyBleRemote::Snapshot shot = remote.snapshot();
  const bool ready = isReadyState(shot.state);
  logSnapshotChange(shot, ready);

  previousRemoteState = shot.state;
  previousConnectedState = shot.connected;
  previousStoredPeerState = shot.hasStoredPeer;
  previousReadyState = ready;
}

void handleConnectedButton() {
  resetLatch = false;

  if (buttonA.wasHold()) {
    stopCue();
    remote.beginFocus();
    return;
  }

  if (buttonA.wasReleasedAfterHold()) {
    remote.endFocus();
    return;
  }

  if (!buttonA.wasClicked()) {
    return;
  }

  const CueAsset shutterCue = selectShutterCue();
  if (playCue(shutterCue.wavData, shutterCue.wavLength, true)) {
    pendingAction = PendingAction::ShutterAfterCue;
    return;
  }

  remote.triggerShutter();
}

void handleDisconnectedButton() {
  if (!resetLatch && buttonA.pressedFor(2000)) {
    stopCue();
    remote.clearPeerAndRestartPairing();
    resetLatch = true;
    return;
  }

  if (buttonA.wasClicked()) {
    playCue(kPairingWav, kPairingWavLen, true);
    return;
  }

  if (buttonA.wasReleased()) {
    resetLatch = false;
  }
}

void handleButton() {
  if (remote.isConnected()) {
    handleConnectedButton();
    return;
  }

  handleDisconnectedButton();
}

}  // namespace

void setup() {
  if (kEnableSerialDebug) {
    Serial.begin(115200);
  }

  pinMode(GPIO_NUM_39, INPUT);

  auto speakerConfig = M5.Speaker.config();
  speakerConfig.pin_data_out = GPIO_NUM_22;
  speakerConfig.pin_bck = GPIO_NUM_19;
  speakerConfig.pin_ws = GPIO_NUM_33;
  speakerConfig.magnification = 12;
  speakerConfig.sample_rate = 24000;
  M5.Speaker.config(speakerConfig);
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);

  remote.begin("ZV-E10");
  updateLedState();
  updateConnectionTracking();
}

void loop() {
  buttonA.setRawState(millis(), digitalRead(GPIO_NUM_39) == LOW);

  remote.loop();
  updateLedState();
  handleButton();
  updatePairingCue();
  triggerPendingActionIfReady();
  updateConnectionTracking();

  delay(5);
}
