#pragma once

#include <Arduino.h>
#include <BLEAddress.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLEScan.h>
#include <BLESecurity.h>
#include <Preferences.h>

class SonyBleRemote {
public:
  enum class State : uint8_t {
    Booting,
    ScanningPair,
    ScanningReconnect,
    Connecting,
    Ready,
    Focusing,
    Shooting,
    Error,
  };

  struct Snapshot {
    State state = State::Booting;
    bool connected = false;
    bool hasStoredPeer = false;
    bool focusing = false;
  };

  SonyBleRemote();
  ~SonyBleRemote();

  void begin(const String& targetName = "Sony");
  void loop();

  bool triggerShutter();
  bool triggerShutterFromFocusHold();
  bool beginFocus();
  bool endFocus();
  void clearPeerAndRestartPairing();

  bool isConnected() const;
  Snapshot snapshot() const;

private:
  enum class ScanMode : uint8_t {
    Pairing,
    Reconnect,
  };

  class ScanCallbacks;
  class ClientCallbacks;
  class SecurityCallbacks;

  void startScanIfNeeded();
  void stopScan();
  void connectPendingDevice();
  void handleDiscoveredDevice(BLEAdvertisedDevice& device);
  bool shouldUseDevice(BLEAdvertisedDevice& device) const;
  bool matchesPairingCandidate(BLEAdvertisedDevice& device) const;
  bool matchesStoredPeer(BLEAdvertisedDevice& device) const;
  bool connectToDevice(BLEAdvertisedDevice& device);
  bool discoverRemote();
  bool writeCommand(const uint8_t* data, size_t length);
  void loadStoredPeer();
  void saveStoredPeer(BLEAdvertisedDevice& device);
  void clearStoredPeer();
  void clearBondedDevices();
  void setState(State state);
  void handleDisconnect();
  void ensureClient();

  static String formatAddress(BLEAddress address);
  static bool hasSonyPairingManufacturerData(BLEAdvertisedDevice& device);

  static constexpr const char* kPrefsNamespace = "sony-remote";
  static constexpr const char* kPrefsKeyAddress = "peer_addr";
  static constexpr const char* kPrefsKeyName = "peer_name";

  static constexpr uint8_t kCmdFocusDown[2] = {0x01, 0x07};
  static constexpr uint8_t kCmdFocusUp[2] = {0x01, 0x06};
  static constexpr uint8_t kCmdShutterDown[2] = {0x01, 0x09};
  static constexpr uint8_t kCmdShutterUp[2] = {0x01, 0x08};

  static constexpr uint32_t kCommandGapMs = 250;
  static constexpr uint32_t kReconnectRetryMs = 1000;

  Preferences preferences_;
  BLEClient* client_ = nullptr;
  BLERemoteCharacteristic* commandCharacteristic_ = nullptr;
  BLEScan* scan_ = nullptr;
  BLEAdvertisedDevice* pendingDevice_ = nullptr;

  ScanCallbacks* scanCallbacks_ = nullptr;
  ClientCallbacks* clientCallbacks_ = nullptr;
  SecurityCallbacks* securityCallbacks_ = nullptr;

  ScanMode scanMode_ = ScanMode::Pairing;
  State state_ = State::Booting;
  String targetName_ = "Sony";
  String storedAddress_;
  String storedName_;

  bool initialized_ = false;
  bool scanActive_ = false;
  bool busy_ = false;
  bool focusing_ = false;
  bool sawAuthSuccess_ = false;
  uint32_t nextRetryAtMs_ = 0;
};
