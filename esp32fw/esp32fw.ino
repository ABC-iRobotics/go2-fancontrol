#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define DEVICE_NAME "go2fan"

// 0xFFFF is the "reserved for testing" company ID, so no Bluetooth SIG
// registration is needed. Advertised little-endian: 0xFF 0xFF.
#define MFG_COMPANY_ID 0xFFFF

// The advertising packet is 31 bytes: flags (3) + name (2 + 7) + manufacturer
// data header (2 + 2 company ID) leaves this many bytes for the payload string.
#define ADV_STRING_MAX (31 - 3 - (2 + sizeof(DEVICE_NAME) - 1) - 4)

#define ADV_UPDATE_MS 1000

static BLEServer *pServer = nullptr;
static BLECharacteristic *pCharacteristic = nullptr;
static BLEAdvertising *pAdvertising = nullptr;
static uint32_t advCounter = 0;
static uint32_t lastAdvUpdate = 0;
static uint8_t fanPwmPin = 5;

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() > 0) {
      Serial.println("*********");
      Serial.print("New value: ");
      Serial.println(value);
      Serial.println("*********");
    }
  }

  void onRead(BLECharacteristic *pCharacteristic) {
    Serial.print("Read: ");
    Serial.println(pCharacteristic->getValue());
  }
};

// Replace the advertising payload with one carrying the given string as
// manufacturer-specific data. Any scanner in range picks it up without
// connecting or pairing. Safe to call while advertising is running.
static void publishAdvString(const String &payload) {
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

static void initFan() {
  pinMode(fanPwmPin, OUTPUT);
}

static void setFanSpeed(const uint8_t percent) {
  
  analogWrite(fanPwmPin, (uint32_t)percent*255/100);
}

void setup() {
  Serial.begin(115200);
  BLEDevice::init(DEVICE_NAME);
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
  // counter in the advertising packet, so it goes in the scan response.
  BLEAdvertisementData scanResponse;
  scanResponse.setCompleteServices(BLEUUID(SERVICE_UUID));
  pAdvertising->setScanResponseData(scanResponse);

  publishAdvString("CNT:0");
  pAdvertising->start();
  Serial.println("BLE advertising started");

  initFan();
}

void loop() {
  if (millis() - lastAdvUpdate >= ADV_UPDATE_MS) {
    lastAdvUpdate = millis();
    String payload = "CNT:" + String(advCounter++);
    publishAdvString(payload);
    Serial.print("Advertising: ");
    Serial.println(payload);
  }
  delay(10);
}
