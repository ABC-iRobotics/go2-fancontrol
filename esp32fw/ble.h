#pragma once

#include <Arduino.h>

// https://www.uuidgenerator.net/
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define DEVICE_NAME "go2fan"

// The BLE address this board advertises with, as "AA:BB:CC:DD:EE:FF" (the
// separators are optional). Every board answers to the same DEVICE_NAME, and
// stock modules out of the same batch can carry the same factory address too,
// which leaves a client with nothing to tell two of them apart; give each board
// its own here. An empty string keeps whatever address the factory programmed.
//   Go2 Jetson unit:        10:00:3B:AF:5D:A2
#define BLE_MAC "10:00:3B:AF:5D:A3"

#define ADV_UPDATE_MS 1000

// A write to the characteristic carries one command line, which is handed to
// the handler the sketch registers with initBle(). Whatever it returns is
// stored back into the characteristic, so a client can write and then read the
// reply back. Called from the BLE task, not from loop().
typedef String (*BleCommandHandler)(const String &line);

// Bring up the GATT server and start advertising.
void initBle(BleCommandHandler handler);

// Replace the advertising payload with one carrying the given string as
// manufacturer-specific data. Any scanner in range picks it up without
// connecting or pairing. Safe to call while advertising is running.
void publishAdvString(const String &payload);
