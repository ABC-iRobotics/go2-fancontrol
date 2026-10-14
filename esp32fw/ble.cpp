#include "ble.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <esp_mac.h>

// 0xFFFF is the "reserved for testing" company ID, so no Bluetooth SIG
// registration is needed. Advertised little-endian: 0xFF 0xFF.
#define MFG_COMPANY_ID 0xFFFF

// The advertising packet is 31 bytes: flags (3) + name (2 + 7) + manufacturer
// data header (2 + 2 company ID) leaves this many bytes for the payload string.
#define ADV_STRING_MAX (31 - 3 - (2 + sizeof(DEVICE_NAME) - 1) - 4)

// Value of one hex digit, or -1 if it is not one.
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parse "AA:BB:CC:DD:EE:FF" into six bytes. Anything that is not a hex digit
// counts as a separator, so "AA-BB-CC-DD-EE-FF" and "AABBCCDDEEFF" parse too.
// Returns false unless the whole string is exactly six octets.
static bool parseMac(const char *text, uint8_t mac[6]) {
  int octets = 0;
  for (const char *p = text; *p != '\0'; p++) {
    int hi = hexVal(*p);
    if (hi < 0) continue; // separator
    int lo = hexVal(p[1]);
    if (lo < 0 || octets == 6) return false; // half an octet, or too many
    mac[octets++] = (uint8_t)(hi << 4 | lo);
    p++; // the loop's own step takes us past the low nibble
  }
  return octets == 6;
}

// Override the address the BLE controller comes up with, so boards sharing a
// factory address can still be told apart. esp_iface_mac_addr_set() replaces
// the interface address outright, rather than the base address the factory one
// is derived from, so it is unaffected by how many MACs this chip hands out.
// The controller reads the address when it starts, so this has to happen before
// BLEDevice::init().
static void applyBleMac() {
  if (BLE_MAC[0] == '\0') return; // not configured, keep the factory address

  uint8_t mac[6];
  if (!parseMac(BLE_MAC, mac)) {
    Serial.println("BLE_MAC is not a MAC address, keeping the factory one");
    return;
  }
  esp_err_t err = esp_iface_mac_addr_set(mac, ESP_MAC_BT);
  if (err != ESP_OK) {
    Serial.printf("BLE_MAC not applied: %s\n", esp_err_to_name(err));
  }
}

static BLEServer *pServer = nullptr;
static BLECharacteristic *pCharacteristic = nullptr;
static BLEAdvertising *pAdvertising = nullptr;
static BleCommandHandler commandHandler = nullptr;

void publishAdvString(const String &payload) {
  String trimmed = payload;
  if (trimmed.length() > ADV_STRING_MAX) {
    trimmed = trimmed.substring(0, ADV_STRING_MAX);
  }

  String mfgData;
  mfgData += (char)(MFG_COMPANY_ID & 0xFF);
  mfgData += (char)(MFG_COMPANY_ID >> 8);
  mfgData += trimmed;

  BLEAdvertisementData advData;
  advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advData.setName(DEVICE_NAME);
  advData.setManufacturerData(mfgData);
  pAdvertising->setAdvertisementData(advData);
}

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    Serial.print("cmd: ");
    Serial.println(value);
    String response = commandHandler ? commandHandler(value)
                                     : String("ERR no command handler");
    Serial.print("rsp: ");
    Serial.println(response);
    // Overwrite the command with its reply, so a read-back returns the answer.
    pCharacteristic->setValue(response);
  }

  void onRead(BLECharacteristic *pCharacteristic) {
    Serial.print("Read: ");
    Serial.println(pCharacteristic->getValue());
  }
};

void initBle(BleCommandHandler handler) {
  commandHandler = handler;

  applyBleMac();
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P15, ESP_BLE_PWR_TYPE_DEFAULT);
  pServer = BLEDevice::createServer();
  // Not the default: without this, advertising stops for good once a client
  // connects and then disconnects.
  pServer->advertiseOnDisconnect(true);

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("Hello World");
  pService->start();

  pAdvertising = pServer->getAdvertising();

  // The 128-bit service UUID needs 18 bytes, which does not fit alongside the
  // payload string in the advertising packet, so it goes in the scan response.
  BLEAdvertisementData scanResponse;
  scanResponse.setCompleteServices(BLEUUID(SERVICE_UUID));
  pAdvertising->setScanResponseData(scanResponse);

  publishAdvString("0%");
  pAdvertising->start();
  // Printed so the address a client has to ask for can be read off the boot
  // log, whether it came from BLE_MAC or from the factory.
  Serial.print("BLE advertising started, address ");
  Serial.println(BLEDevice::getAddress().toString().c_str());
}
