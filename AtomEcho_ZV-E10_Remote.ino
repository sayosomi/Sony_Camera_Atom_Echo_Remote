#include <M5Unified.h>
#include <esp_system.h>

#include "AudioAssets.h"
#include "ShutterVariants.h"
#include "SonyBleRemote.h"

namespace {

SonyBleRemote remote;

struct CueAsset {
  const uint8_t* wavData;
  size_t wavLength;
};

enum class PendingAction : uint8_t {
  None,
  ShutterAfterCue,
};

PendingAction pendingAction = PendingAction::None;
bool resetLatch = false;
bool hadStoredPeerAtBoot = false;
bool pairedCuePlayedThisBoot = false;
int8_t lastShutterCueIndex = -1;

constexpr CueAsset kShutterCueOptions[] = {
  {kShutterWav, kShutterWavLen},
  {kShutter2Wav, kShutter2WavLen},
  {kShutter3Wav, kShutter3WavLen},
};
constexpr size_t kShutterCueCount =
    sizeof(kShutterCueOptions) / sizeof(kShutterCueOptions[0]);

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
  if (hadStoredPeerAtBoot || pairedCuePlayedThisBoot) {
    return;
  }

  const SonyBleRemote::Snapshot shot = remote.snapshot();
  if (!shot.connected || !shot.hasStoredPeer) {
    return;
  }

  pairedCuePlayedThisBoot = true;
  playCue(kPairedWav, kPairedWavLen, true);
}

void handleConnectedButton() {
  resetLatch = false;

  if (M5.BtnA.wasHold()) {
    stopCue();
    remote.beginFocus();
    return;
  }

  if (M5.BtnA.wasReleasedAfterHold()) {
    remote.endFocus();
    return;
  }

  if (!M5.BtnA.wasClicked()) {
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
  if (!resetLatch && M5.BtnA.pressedFor(2000)) {
    stopCue();
    remote.clearPeerAndRestartPairing();
    resetLatch = true;
    return;
  }

  if (M5.BtnA.wasClicked()) {
    playCue(kPairingWav, kPairingWavLen, true);
    return;
  }

  if (M5.BtnA.wasReleased()) {
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
  auto cfg = M5.config();
  cfg.internal_mic = false;
  M5.begin(cfg);

  auto speakerConfig = M5.Speaker.config();
  speakerConfig.sample_rate = 24000;
  M5.Speaker.config(speakerConfig);
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);

  remote.begin("ZV-E10");
  hadStoredPeerAtBoot = remote.snapshot().hasStoredPeer;
}

void loop() {
  M5.update();

  remote.loop();
  handleButton();
  updatePairingCue();
  triggerPendingActionIfReady();

  delay(5);
}
