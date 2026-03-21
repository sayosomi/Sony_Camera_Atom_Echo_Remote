#include "SonyBleRemote.h"

#include <algorithm>
#include <vector>

#include <esp_gap_ble_api.h>

namespace {

constexpr char kServiceUuid[] = "8000ff00-ff00-ffff-ffff-ffffffffffff";
constexpr char kCommandUuid[] = "0000ff01-0000-1000-8000-00805f9b34fb";

bool containsIgnoreCase(const String& haystack, const String& needle) {
  if (needle.isEmpty()) {
    return true;
  }

  String left = haystack;
  String right = needle;
  left.toLowerCase();
  right.toLowerCase();
  return left.indexOf(right) >= 0;
}

}  // namespace

class SonyBleRemote::ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  explicit ScanCallbacks(SonyBleRemote& owner) : owner_(owner) {}

  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    owner_.handleDiscoveredDevice(advertisedDevice);
  }

private:
  SonyBleRemote& owner_;
};

class SonyBleRemote::ClientCallbacks : public BLEClientCallbacks {
public:
  explicit ClientCallbacks(SonyBleRemote& owner) : owner_(owner) {}

  void onConnect(BLEClient* client) override {
    (void)client;
  }

  void onDisconnect(BLEClient* client) override {
    (void)client;
    owner_.handleDisconnect();
  }

private:
  SonyBleRemote& owner_;
};

class SonyBleRemote::SecurityCallbacks : public BLESecurityCallbacks {
public:
  explicit SecurityCallbacks(SonyBleRemote& owner) : owner_(owner) {}

  uint32_t onPassKeyRequest() override {
    return 0;
  }

  void onPassKeyNotify(uint32_t pass_key) override {
    (void)pass_key;
    owner_.setState(owner_.state_);
  }

  bool onSecurityRequest() override {
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) override {
    if (auth_cmpl.success) {
      owner_.sawAuthSuccess_ = true;
      owner_.setState(owner_.state_);
      return;
    }

    (void)auth_cmpl;
    owner_.setState(State::Error);
  }

  bool onConfirmPIN(uint32_t pin) override {
    (void)pin;
    owner_.setState(owner_.state_);
    return true;
  }

private:
  SonyBleRemote& owner_;
};

SonyBleRemote::SonyBleRemote() = default;

SonyBleRemote::~SonyBleRemote() {
  stopScan();

  if (client_ != nullptr && client_->isConnected()) {
    client_->disconnect();
  }

  delete pendingDevice_;
}

void SonyBleRemote::begin(const String& targetName) {
  if (initialized_) {
    return;
  }

  targetName_ = targetName;
  loadStoredPeer();

  BLEDevice::init("AtomEcho-ZV-E10");
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

  securityCallbacks_ = new SecurityCallbacks(*this);
  BLEDevice::setSecurityCallbacks(securityCallbacks_);

  BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setKeySize(16);

  scanCallbacks_ = new ScanCallbacks(*this);
  clientCallbacks_ = new ClientCallbacks(*this);

  scan_ = BLEDevice::getScan();
  scan_->setAdvertisedDeviceCallbacks(scanCallbacks_, false, true);
  scan_->setInterval(1349);
  scan_->setWindow(449);
  scan_->setActiveScan(true);

  scanMode_ = storedAddress_.isEmpty() ? ScanMode::Pairing : ScanMode::Reconnect;
  setState(scanMode_ == ScanMode::Pairing ? State::ScanningPair : State::ScanningReconnect);

  initialized_ = true;
}

void SonyBleRemote::loop() {
  if (!initialized_) {
    return;
  }

  if (pendingDevice_ != nullptr && !busy_) {
    connectPendingDevice();
  }

  if (client_ != nullptr && !client_->isConnected() && state_ != State::ScanningPair &&
      state_ != State::ScanningReconnect && pendingDevice_ == nullptr) {
    commandCharacteristic_ = nullptr;
    focusing_ = false;
    busy_ = false;
  }

  if (millis() >= nextRetryAtMs_ && !isConnected() && pendingDevice_ == nullptr) {
    startScanIfNeeded();
  }
}

bool SonyBleRemote::triggerShutter() {
  if (!isConnected() || busy_) {
    return false;
  }

  busy_ = true;
  focusing_ = false;
  setState(State::Shooting);

  const bool ok =
      writeCommand(kCmdFocusDown, sizeof(kCmdFocusDown)) &&
      writeCommand(kCmdShutterDown, sizeof(kCmdShutterDown)) &&
      writeCommand(kCmdShutterUp, sizeof(kCmdShutterUp)) &&
      writeCommand(kCmdFocusUp, sizeof(kCmdFocusUp));

  busy_ = false;

  if (ok) {
    setState(State::Ready);
    return true;
  }

  setState(State::Error);
  return false;
}

bool SonyBleRemote::triggerShutterFromFocusHold() {
  if (!isConnected() || busy_ || !focusing_) {
    return false;
  }

  busy_ = true;
  setState(State::Shooting);

  const bool ok =
      writeCommand(kCmdShutterDown, sizeof(kCmdShutterDown)) &&
      writeCommand(kCmdShutterUp, sizeof(kCmdShutterUp)) &&
      writeCommand(kCmdFocusUp, sizeof(kCmdFocusUp));

  busy_ = false;
  focusing_ = false;

  if (ok) {
    setState(State::Ready);
    return true;
  }

  setState(State::Error);
  return false;
}

bool SonyBleRemote::beginFocus() {
  if (!isConnected() || busy_ || focusing_) {
    return false;
  }

  busy_ = true;
  setState(State::Focusing);
  const bool ok = writeCommand(kCmdFocusDown, sizeof(kCmdFocusDown));
  busy_ = false;

  if (!ok) {
    setState(State::Error);
    return false;
  }

  focusing_ = true;
  setState(State::Focusing);
  return true;
}

bool SonyBleRemote::endFocus() {
  if (!isConnected() || busy_ || !focusing_) {
    return false;
  }

  busy_ = true;
  setState(State::Ready);
  const bool ok = writeCommand(kCmdFocusUp, sizeof(kCmdFocusUp));
  busy_ = false;
  focusing_ = false;

  if (!ok) {
    setState(State::Error);
    return false;
  }

  setState(State::Ready);
  return true;
}

void SonyBleRemote::clearPeerAndRestartPairing() {
  stopScan();

  if (client_ != nullptr && client_->isConnected()) {
    client_->disconnect();
  }

  commandCharacteristic_ = nullptr;
  focusing_ = false;
  busy_ = false;
  delete pendingDevice_;
  pendingDevice_ = nullptr;

  clearStoredPeer();
  clearBondedDevices();

  scanMode_ = ScanMode::Pairing;
  nextRetryAtMs_ = 0;
  setState(State::ScanningPair);
}

bool SonyBleRemote::isConnected() const {
  return client_ != nullptr && client_->isConnected() && commandCharacteristic_ != nullptr;
}

SonyBleRemote::Snapshot SonyBleRemote::snapshot() const {
  Snapshot shot;
  shot.state = state_;
  shot.connected = isConnected();
  shot.hasStoredPeer = !storedAddress_.isEmpty();
  shot.focusing = focusing_;
  return shot;
}

void SonyBleRemote::startScanIfNeeded() {
  if (scan_ == nullptr || scanActive_) {
    return;
  }

  scanMode_ = storedAddress_.isEmpty() ? ScanMode::Pairing : ScanMode::Reconnect;
  setState(scanMode_ == ScanMode::Pairing ? State::ScanningPair : State::ScanningReconnect);

  if (!scan_->start(0, nullptr, false)) {
    setState(State::Error);
    nextRetryAtMs_ = millis() + kReconnectRetryMs;
    return;
  }

  scanActive_ = true;
}

void SonyBleRemote::stopScan() {
  if (scan_ == nullptr || !scanActive_) {
    return;
  }

  scan_->stop();
  scan_->clearResults();
  scanActive_ = false;
}

void SonyBleRemote::connectPendingDevice() {
  BLEAdvertisedDevice device(*pendingDevice_);
  delete pendingDevice_;
  pendingDevice_ = nullptr;

  if (!connectToDevice(device)) {
    commandCharacteristic_ = nullptr;
    nextRetryAtMs_ = millis() + kReconnectRetryMs;
    scanMode_ = storedAddress_.isEmpty() ? ScanMode::Pairing : ScanMode::Reconnect;
    setState(State::Error);
  }
}

void SonyBleRemote::handleDiscoveredDevice(BLEAdvertisedDevice& device) {
  if (!shouldUseDevice(device)) {
    return;
  }

  stopScan();
  delete pendingDevice_;
  pendingDevice_ = new BLEAdvertisedDevice(device);
}

bool SonyBleRemote::shouldUseDevice(BLEAdvertisedDevice& device) const {
  return scanMode_ == ScanMode::Reconnect ? matchesStoredPeer(device) : matchesPairingCandidate(device);
}

bool SonyBleRemote::matchesPairingCandidate(BLEAdvertisedDevice& device) const {
  const bool hasTargetName = device.haveName() && containsIgnoreCase(device.getName(), targetName_);
  const bool hasSonyPairingData = hasSonyPairingManufacturerData(device);
  return hasTargetName || hasSonyPairingData;
}

bool SonyBleRemote::matchesStoredPeer(BLEAdvertisedDevice& device) const {
  if (!storedAddress_.isEmpty() && formatAddress(device.getAddress()).equalsIgnoreCase(storedAddress_)) {
    return true;
  }

  if (!storedName_.isEmpty() && device.haveName() && containsIgnoreCase(device.getName(), storedName_)) {
    return true;
  }

  if (device.haveName() && containsIgnoreCase(device.getName(), targetName_)) {
    return true;
  }

  return false;
}

bool SonyBleRemote::connectToDevice(BLEAdvertisedDevice& device) {
  ensureClient();
  if (client_ == nullptr) {
    setState(State::Error);
    return false;
  }

  setState(State::Connecting);

  if (!client_->connect(const_cast<BLEAdvertisedDevice*>(&device))) {
    setState(State::Error);
    return false;
  }

  client_->setMTU(185);

  if (!discoverRemote()) {
    client_->disconnect();
    return false;
  }

  saveStoredPeer(device);
  setState(State::Ready);
  return true;
}

bool SonyBleRemote::discoverRemote() {
  BLERemoteService* service = client_->getService(kServiceUuid);
  if (service == nullptr) {
    setState(State::Error);
    return false;
  }

  commandCharacteristic_ = service->getCharacteristic(kCommandUuid);
  if (commandCharacteristic_ == nullptr) {
    setState(State::Error);
    return false;
  }

  if (!commandCharacteristic_->canWrite() && !commandCharacteristic_->canWriteNoResponse()) {
    setState(State::Error);
    commandCharacteristic_ = nullptr;
    return false;
  }

  return true;
}

bool SonyBleRemote::writeCommand(const uint8_t* data, size_t length) {
  if (!isConnected()) {
    return false;
  }

  commandCharacteristic_->writeValue(const_cast<uint8_t*>(data), length, true);
  delay(kCommandGapMs);
  return true;
}

void SonyBleRemote::loadStoredPeer() {
  preferences_.begin(kPrefsNamespace, true);
  storedAddress_ = preferences_.getString(kPrefsKeyAddress, "");
  storedName_ = preferences_.getString(kPrefsKeyName, "");
  preferences_.end();
}

void SonyBleRemote::saveStoredPeer(BLEAdvertisedDevice& device) {
  storedAddress_ = formatAddress(device.getAddress());
  storedName_ = device.haveName() ? device.getName() : storedName_;

  preferences_.begin(kPrefsNamespace, false);
  preferences_.putString(kPrefsKeyAddress, storedAddress_);
  preferences_.putString(kPrefsKeyName, storedName_);
  preferences_.end();
}

void SonyBleRemote::clearStoredPeer() {
  preferences_.begin(kPrefsNamespace, false);
  preferences_.clear();
  preferences_.end();

  storedAddress_.clear();
  storedName_.clear();
}

void SonyBleRemote::clearBondedDevices() {
  int deviceCount = esp_ble_get_bond_device_num();
  if (deviceCount <= 0) {
    return;
  }

  std::vector<esp_ble_bond_dev_t> bonded(static_cast<size_t>(deviceCount));
  if (esp_ble_get_bond_device_list(&deviceCount, bonded.data()) != ESP_OK) {
    return;
  }

  for (int index = 0; index < deviceCount; ++index) {
    esp_ble_remove_bond_device(bonded[index].bd_addr);
  }
}

void SonyBleRemote::setState(State state) {
  state_ = state;
}

void SonyBleRemote::handleDisconnect() {
  commandCharacteristic_ = nullptr;
  focusing_ = false;
  busy_ = false;
  nextRetryAtMs_ = millis() + kReconnectRetryMs;

  if (storedAddress_.isEmpty()) {
    setState(State::ScanningPair);
    return;
  }

  setState(State::ScanningReconnect);
}

void SonyBleRemote::ensureClient() {
  if (client_ != nullptr) {
    return;
  }

  client_ = BLEDevice::createClient();
  client_->setClientCallbacks(clientCallbacks_);
}

String SonyBleRemote::formatAddress(BLEAddress address) {
  String text = address.toString().c_str();
  text.toLowerCase();
  return text;
}

bool SonyBleRemote::hasSonyPairingManufacturerData(BLEAdvertisedDevice& device) {
  if (!device.haveManufacturerData()) {
    return false;
  }

  const String data = device.getManufacturerData();
  if (data.length() < 8) {
    return false;
  }

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.c_str());
  return bytes[0] == 0x2d && bytes[1] == 0x01 && bytes[5] == 0x22 && bytes[6] == 0xef && bytes[7] == 0x00;
}
