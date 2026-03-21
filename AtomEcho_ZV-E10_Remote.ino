#include <M5Unified.h>

#include "AudioAssets.h"
#include "SonyBleRemote.h"

namespace {

SonyBleRemote remote;

enum class PendingAction : uint8_t {
  None,
  ShutterAfterCue,
};

PendingAction pendingAction = PendingAction::None;
bool resetLatch = false;
bool hadStoredPeerAtBoot = false;
bool pairedCuePlayedThisBoot = false;

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

  if (playCue(kShutterWav, kShutterWavLen, true)) {
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
