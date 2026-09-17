#!/usr/bin/env python3
"""Read/write the ESP32 BLE characteristic and watch its advertised counter.

Needs bleak:  pip install bleak

Usage:
  ./ble_client.py scan                 # show the advertised counter string
  ./ble_client.py read                 # read the characteristic
  ./ble_client.py write "some string"  # write it, then read it back
"""

import asyncio
import sys

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "go2fan"
CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
COMPANY_ID = 0xFFFF


async def find_device():
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10.0)
    if device is None:
        sys.exit(f"{DEVICE_NAME} not found")
    return device


async def scan():
    """Print the manufacturer-data string from each advertisement, no connection."""

    def callback(device, adv):
        if device.name != DEVICE_NAME:
            return
        payload = adv.manufacturer_data.get(COMPANY_ID)
        if payload is not None:
            print(f"{device.address}  rssi={adv.rssi:4d}  {payload.decode(errors='replace')}")

    async with BleakScanner(callback):
        print("Scanning, Ctrl-C to stop...")
        await asyncio.Event().wait()


async def read():
    device = await find_device()
    async with BleakClient(device) as client:
        value = await client.read_gatt_char(CHARACTERISTIC_UUID)
        print(value.decode(errors="replace"))


async def write(text):
    device = await find_device()
    async with BleakClient(device) as client:
        await client.write_gatt_char(CHARACTERISTIC_UUID, text.encode(), response=True)
        value = await client.read_gatt_char(CHARACTERISTIC_UUID)
        print(f"wrote {text!r}, read back {value.decode(errors='replace')!r}")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "scan"
    try:
        if cmd == "scan":
            asyncio.run(scan())
        elif cmd == "read":
            asyncio.run(read())
        elif cmd == "write":
            asyncio.run(write(sys.argv[2]))
        else:
            sys.exit(__doc__)
    except KeyboardInterrupt:
        pass
