#!/usr/bin/env python3
"""Read/write the ESP32 BLE characteristic and watch its advertised counter.

Needs bleak; --log needs mcap and --loglive needs foxglove-sdk:
  pip install bleak mcap foxglove-sdk

Usage:
  ./ble_client.py scan                 # show the advertised fan/temperature data
  ./ble_client.py scan --log [FILE]    # ...and log it to an MCAP file for Foxglove
  ./ble_client.py scan --loglive       # ...and serve it live to the Foxglove app
  ./ble_client.py read                 # read the characteristic
  ./ble_client.py write "some string"  # write it, then read it back

With more than one unit in range, pick one by MAC address:
  ./ble_client.py --address 10:00:3B:AF:09:36 read
  GO2FAN_ADDRESS=10:00:3B:AF:09:36 ./ble_client.py scan
"""

import argparse
import asyncio
import json
import math
import os
import re
import sys
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

DEVICE_NAME = "go2fan"

# Which unit to talk to when several are in range: every one of them answers to
# DEVICE_NAME, so the MAC address is what tells them apart. None means the first
# one that turns up, which is what a single-device setup wants. Override it per
# command with --address, for a whole shell with GO2FAN_ADDRESS, or permanently
# by filling this in.
#   Go2 mainboard unit: 10:00:3B:AF:09:36 (the stock one)
#   Jetson unit:        10:00:3B:AF:5D:A2
DEFAULT_ADDRESS = None
ADDRESS_ENV = "GO2FAN_ADDRESS"

CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
COMPANY_ID = 0xFFFF

# The firmware refreshes the advertisement once a second, but the same payload
# is broadcast (and received) many times in between, so the log is thinned to
# one datapoint per second.
LOG_INTERVAL_S = 1.0

# Foxglove plots whatever numeric fields it finds in a JSON message; this
# schema is what tells it they are numbers before any message has arrived.
SAMPLE_SCHEMA = {
    "type": "object",
    "title": "go2fan.Sample",
    "properties": {
        "temperature_c": {"type": "number", "description": "heatsink temperature"},
        "fan_percent": {"type": "number", "description": "fan speed in percent"},
        "rssi": {"type": "number", "description": "advertisement RSSI in dBm"},
    },
}
SAMPLE_TOPIC = "/go2fan"

# Default port of the live WebSocket server, the one the Foxglove app offers.
LIVE_PORT = 8765


def normalize_address(text):
    """Fold a MAC into bare upper-case hex, so the separators need not match.

    "10:00:3b:af:09:36", "10-00-3B-AF-09-36" and "10003baf0936" are the same
    address. Anything that is not twelve hex digits is left alone apart from the
    case, because bleak reports a UUID rather than a MAC on macOS.
    """
    stripped = re.sub(r"[:\-\s]", "", text).upper()
    if re.fullmatch(r"[0-9A-F]{12}", stripped):
        return stripped
    return text.strip().upper()


def matches_address(device_address, wanted):
    """True if this is the device asked for; no address asked for matches all."""
    return wanted is None or normalize_address(device_address) == normalize_address(
        wanted
    )


def parse_adv(payload):
    """Split "45.3C|60%" into (temperature, fan percent). Either may be None.

    The firmware advertises "nanC" until the sensor has been read once, and
    NaN has no JSON spelling, so anything not finite is dropped here.
    """
    temperature = fan = None
    for field in payload.split("|"):
        field = field.strip()
        try:
            if field.endswith("C"):
                temperature = float(field[:-1])
            elif field.endswith("%"):
                fan = float(field[:-1])
        except ValueError:
            pass  # a garbled advertisement is not worth aborting the scan for
    if temperature is not None and not math.isfinite(temperature):
        temperature = None
    if fan is not None and not math.isfinite(fan):
        fan = None
    return temperature, fan


def build_sample(temperature, fan, rssi):
    """The message both sinks log. Absent fields are left out, not sent as null."""
    sample = {"rssi": rssi}
    if temperature is not None:
        sample["temperature_c"] = temperature
    if fan is not None:
        sample["fan_percent"] = fan
    return sample


class AdvLogger:
    """Writes advertised samples to an MCAP file Foxglove can open directly.

    Created on the first advertisement rather than at startup, so the device
    address is known and can go into the file name and the metadata.
    """

    def __init__(self, path, device_name, address):
        from mcap.writer import Writer

        self.path = path
        self._file = open(path, "wb")
        self._writer = Writer(self._file)
        self._writer.start(profile="", library="go2-fancontrol ble_client.py")
        self._writer.add_metadata(
            "device",
            {
                "ble_name": device_name,
                "ble_address": address,
                "characteristic_uuid": CHARACTERISTIC_UUID,
                "started": datetime.now().astimezone().isoformat(),
            },
        )
        schema_id = self._writer.register_schema(
            name="go2fan.Sample",
            encoding="jsonschema",
            data=json.dumps(SAMPLE_SCHEMA).encode(),
        )
        self._channel_id = self._writer.register_channel(
            topic=SAMPLE_TOPIC, message_encoding="json", schema_id=schema_id
        )

    @staticmethod
    def default_path(device_name, address):
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        return f"{device_name}-{address.replace(':', '')}-{stamp}.mcap"

    def log(self, sample):
        now = time.time_ns()
        self._writer.add_message(
            channel_id=self._channel_id,
            log_time=now,
            publish_time=now,
            data=json.dumps(sample).encode(),
        )

    def close(self):
        self._writer.finish()
        self._file.close()


class LiveLogger:
    """Serves the same samples to the Foxglove app over a WebSocket.

    The server is started up front, so the app can be connected while waiting
    for the device; the channel carrying the samples only appears once the
    first advertisement has named the device it is coming from.
    """

    def __init__(self, port=LIVE_PORT, host="127.0.0.1"):
        import foxglove

        # The SDK narrates every connection at INFO, which would be mixed into
        # the advertisement lines.
        foxglove.set_log_level("WARN")
        self._foxglove = foxglove
        self._server = foxglove.start_server(
            name=f"{DEVICE_NAME} ble_client", host=host, port=port
        )
        self._channel = None
        self.port = self._server.port
        self.url = self._server.app_url()

    def bind(self, device_name, address):
        """Create the channel, now that the device the samples come from is known."""
        self._channel = self._foxglove.Channel(
            SAMPLE_TOPIC,
            schema=SAMPLE_SCHEMA,
            metadata={
                "ble_name": device_name,
                "ble_address": address,
                "characteristic_uuid": CHARACTERISTIC_UUID,
            },
        )

    def log(self, sample):
        self._channel.log(sample, log_time=time.time_ns())

    def close(self):
        self._server.stop()


async def find_device(address=None):
    """The go2fan at the given address, or any go2fan if no address is given."""

    def is_wanted(device, adv):
        if DEVICE_NAME not in (device.name, adv.local_name):
            return False
        return matches_address(device.address, address)

    device = await BleakScanner.find_device_by_filter(is_wanted, timeout=10.0)
    if device is None:
        sys.exit(f"{DEVICE_NAME}{f' at {address}' if address else ''} not found")
    return device


async def scan(address=None, log_path=None, logging_on=False, live_port=None):
    """Print the manufacturer-data string from each advertisement, no connection.

    With an address only that unit is shown, which is what keeps two concurrent
    scans of two units apart. With logging_on a datapoint goes into an MCAP
    file every LOG_INTERVAL_S;
    log_path of None means one is named after the device and the date. With
    live_port the same datapoints are also served to the Foxglove app. Both
    sinks can run at once.
    """
    live = LiveLogger(live_port) if live_port is not None else None
    if live is not None:
        print(f"live: serving on ws://127.0.0.1:{live.port}")
        print(f"      open {live.url}")

    state = {"file": None, "bound": False, "last": 0.0}

    def callback(device, adv):
        if DEVICE_NAME not in (device.name, adv.local_name):
            return
        if not matches_address(device.address, address):
            return
        payload = adv.manufacturer_data.get(COMPANY_ID)
        if payload is None:
            return
        text = payload.decode(errors="replace")

        sinks_on = logging_on or live is not None
        due = sinks_on and time.monotonic() - state["last"] >= LOG_INTERVAL_S
        if due:
            # Both sinks want the device address, which only an advertisement
            # can supply, so neither is set up before the first one arrives.
            if not state["bound"]:
                if logging_on:
                    path = log_path or AdvLogger.default_path(
                        DEVICE_NAME, device.address
                    )
                    state["file"] = AdvLogger(path, DEVICE_NAME, device.address)
                    print(f"logging to {path}")
                if live is not None:
                    live.bind(DEVICE_NAME, device.address)
                state["bound"] = True
            sample = build_sample(*parse_adv(text), adv.rssi)
            if state["file"] is not None:
                state["file"].log(sample)
            if live is not None:
                live.log(sample)
            state["last"] = time.monotonic()

        prefix = "(logging) " if due else ""
        print(f"{prefix}{device.address}  rssi={adv.rssi:4d}  {text}")

    try:
        async with BleakScanner(callback):
            print(
                f"Scanning for {DEVICE_NAME}"
                f"{f' at {address}' if address else ''}, Ctrl-C to stop..."
            )
            await asyncio.Event().wait()
    finally:
        if state["file"] is not None:
            state["file"].close()
            print(f"wrote {state['file'].path}")
        if live is not None:
            live.close()


CONNECT_ATTEMPTS = 5
CONNECT_TIMEOUT = 20.0


async def connect(address=None):
    """A connected client for the go2fan, retrying while the link is weak.

    At around -90 dBm the connection request or the first packets after it get
    lost often enough that a single attempt with bleak's 10 s default times out,
    while the next attempt goes through. So wait longer, and try again.
    """
    device = await find_device(address)
    for attempt in range(1, CONNECT_ATTEMPTS + 1):
        client = BleakClient(device, timeout=CONNECT_TIMEOUT)
        try:
            await client.connect()
            return client
        except (TimeoutError, BleakError) as e:
            print(
                f"connect attempt {attempt}/{CONNECT_ATTEMPTS} failed: "
                f"{e or type(e).__name__}",
                file=sys.stderr,
            )
            if attempt == CONNECT_ATTEMPTS:
                sys.exit(f"could not connect to {device.address}")
            await asyncio.sleep(1.0)


async def read(address=None):
    client = await connect(address)
    try:
        value = await client.read_gatt_char(CHARACTERISTIC_UUID)
        print(value.decode(errors="replace"))
    finally:
        await client.disconnect()


async def write(text, address=None):
    client = await connect(address)
    try:
        await client.write_gatt_char(CHARACTERISTIC_UUID, text.encode(), response=True)
        value = await client.read_gatt_char(CHARACTERISTIC_UUID)
        print(f"wrote {text!r}, read back {value.decode(errors='replace')!r}")
    finally:
        await client.disconnect()


def main():
    # --address is accepted on either side of the sub-command, which means both
    # parsers define it; SUPPRESS keeps the one that was not used from writing
    # its default over the one that was.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "-a",
        "--address",
        metavar="MAC",
        default=argparse.SUPPRESS,
        help=f"MAC address of the unit to talk to, for when several are in "
        f"range (default: ${ADDRESS_ENV}, else the first one found)",
    )

    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0], parents=[common]
    )
    sub = parser.add_subparsers(dest="cmd")

    p_scan = sub.add_parser("scan", parents=[common], help="watch the advertised data")
    # nargs="?" so both "--log" and "--log run.mcap" work; the flag being
    # absent leaves this None as well, hence the separate default.
    p_scan.add_argument(
        "--log",
        nargs="?",
        const="",
        default=None,
        metavar="FILE",
        help="log samples to FILE (MCAP, for Foxglove); "
        "without FILE the name is built from the device name and the date",
    )
    p_scan.add_argument(
        "--loglive",
        nargs="?",
        const=LIVE_PORT,
        type=int,
        default=None,
        metavar="PORT",
        help=f"serve samples live to the Foxglove app over a WebSocket "
        f"on PORT (default {LIVE_PORT})",
    )
    sub.add_parser("read", parents=[common], help="read the characteristic")
    p_write = sub.add_parser(
        "write", parents=[common], help="write the characteristic, read it back"
    )
    p_write.add_argument("text")

    args = parser.parse_args(sys.argv[1:] or ["scan"])
    address = (
        getattr(args, "address", None) or os.environ.get(ADDRESS_ENV) or DEFAULT_ADDRESS
    )

    if args.cmd == "read":
        asyncio.run(read(address))
    elif args.cmd == "write":
        asyncio.run(write(args.text, address))
    else:
        log = getattr(args, "log", None)
        asyncio.run(
            scan(
                address=address,
                log_path=log or None,
                logging_on=log is not None,
                live_port=getattr(args, "loglive", None),
            )
        )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
