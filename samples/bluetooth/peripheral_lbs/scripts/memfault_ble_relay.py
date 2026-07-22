#!/usr/bin/env -S uv run
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
# /// script
# requires-python = ">=3.9"
# dependencies = [
#     "bleak>=0.21",
#     "cbor2>=5.4",
#     "requests>=2.28",
# ]
# ///
"""Relay CBOR events from the peripheral_lbs sample to the Memfault events API.

This script connects over Bluetooth LE to a device running the
peripheral_lbs sample (with the Memfault Event Service enabled), subscribes
to notifications on the Memfault Event Data characteristic, and forwards each
received CBOR payload as a "heartbeat" event to the Memfault events API:

    https://api-docs.memfault.com/#7267d576-ee78-4d1c-b7a3-8fc4dd82ce04

The device identity (device serial, software type, software version, and
hardware version) is read from the CBOR payload itself - the device is
treated as an opaque downstream unit that self-reports its own identity,
rather than something this script needs to be told about.

Not every BLE central negotiates an ATT MTU large enough to fit the whole
CBOR payload in a single notification, so the device splits each event into
fragments (see memfault_event.c). This script reassembles them before
decoding.

Example (run directly with uv, which installs the dependencies declared in
the inline script metadata above into an ephemeral environment):

    uv run memfault_ble_relay.py \\
        --project-key <your-memfault-project-key> \\
        --device-name Nordic_LBS
"""

import argparse
import asyncio
import json
import logging
import sys
import uuid

import cbor2
import requests
from bleak import BleakClient, BleakScanner

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger(__name__)

MEMFAULT_EVENT_SVC_UUID = uuid.UUID("00001526-1212-efde-1523-785feabcd123")
MEMFAULT_EVENT_DATA_UUID = uuid.UUID("00001527-1212-efde-1523-785feabcd123")

DEFAULT_EVENTS_URL = "https://device.memfault.com/api/v0/events"
# The events API rejects events without this exact sdk_version, even though
# this script isn't using the actual Memfault firmware SDK.
SDK_VERSION = "0.5.0"

# Matches the fragment header format in memfault_event.c: 1 byte per
# fragment, low 7 bits are the 0-based fragment index, top bit marks the
# last fragment of the event.
FRAG_LAST_BIT = 0x80
FRAG_INDEX_MASK = 0x7F


class EventReassembler:
    """Reassembles fragmented Memfault event notifications.

    Fragments are buffered by index as they arrive and concatenated in
    order once the last fragment (identified by FRAG_LAST_BIT) is seen.
    """

    def __init__(self):
        self._fragments = {}

    def add_fragment(self, data: bytes):
        """Add a fragment. Returns the reassembled payload once complete, else None."""
        header = data[0]
        index = header & FRAG_INDEX_MASK
        is_last = bool(header & FRAG_LAST_BIT)

        self._fragments[index] = data[1:]

        if not is_last:
            return None

        try:
            payload = b"".join(self._fragments[i] for i in range(len(self._fragments)))
        except KeyError:
            log.error("Dropping event: missing fragment(s)")
            payload = None
        finally:
            self._fragments.clear()

        return payload


def parse_args():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--project-key",
        required=True,
        help="Memfault project key.",
    )
    parser.add_argument(
        "--events-url",
        default=DEFAULT_EVENTS_URL,
        help=f"Memfault events API URL (default: {DEFAULT_EVENTS_URL}).",
    )
    parser.add_argument(
        "--device-name",
        default="Nordic_LBS",
        help="BLE advertised name to scan for (default: Nordic_LBS).",
    )
    parser.add_argument(
        "--scan-timeout",
        type=float,
        default=10.0,
        help="Seconds to scan for the device before giving up (default: 10).",
    )
    return parser.parse_args()


def build_event(cbor_payload, rssi_dbm):
    """Translate a decoded CBOR payload into a Memfault heartbeat event.

    The device identity fields are read straight from the payload, since the
    device is the authority on its own identity.
    """
    metrics = {
        "rssi_dbm": rssi_dbm,
        "batt_v": cbor_payload["batt_mv"] / 1000.0,
        "button_presses": cbor_payload["button_presses"],
        "uptime_s": cbor_payload["uptime_s"],
    }

    return {
        "type": "heartbeat",
        "sdk_version": SDK_VERSION,
        "software_type": cbor_payload["software_type"],
        "software_version": cbor_payload["software_version"],
        "device_serial": cbor_payload["device_serial"],
        "hardware_version": cbor_payload["hardware_version"],
        "event_info": {
            "metrics": metrics,
            "report_type": "ble_relay",
        },
        "user_info": {},
    }


def post_event(events_url, project_key, event):
    log.info("Posting event: %s", json.dumps(event))

    response = requests.post(
        events_url,
        headers={
            "Content-Type": "application/json",
            "Memfault-Project-Key": project_key,
        },
        json=[event],
        timeout=10,
    )

    if response.ok:
        log.info("Post succeeded")
    else:
        log.error("Failed to post event: %s %s", response.status_code, response.text)


async def main():
    args = parse_args()

    log.info("Scanning for '%s'...", args.device_name)
    discovered = await BleakScanner.discover(timeout=args.scan_timeout, return_adv=True)
    match = next(
        (
            (dev, adv)
            for dev, adv in discovered.values()
            if adv.local_name == args.device_name
        ),
        None,
    )
    if match is None:
        log.error("Device '%s' not found.", args.device_name)
        sys.exit(1)
    device, advertisement_data = match

    # A live RSSI reading isn't available once connected (the peripheral stops
    # advertising after a connection is established), so the value seen during
    # the connection scan is used as a representative signal strength.
    rssi_dbm = advertisement_data.rssi

    log.info("Connecting to %s...", device.address)
    async with BleakClient(device) as client:
        log.info("Connected. Waiting for Memfault event notifications (Ctrl+C to stop)...")

        reassembler = EventReassembler()

        def notification_handler(_characteristic, data: bytearray):
            cbor_bytes = reassembler.add_fragment(bytes(data))
            if cbor_bytes is None:
                return

            try:
                payload = cbor2.loads(cbor_bytes)
            except cbor2.CBORDecodeError as exc:
                log.error("Failed to decode CBOR payload: %s", exc)
                return

            event = build_event(payload, rssi_dbm)
            post_event(args.events_url, args.project_key, event)

        await client.start_notify(MEMFAULT_EVENT_DATA_UUID, notification_handler)

        try:
            while client.is_connected:
                await asyncio.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            await client.stop_notify(MEMFAULT_EVENT_DATA_UUID)


if __name__ == "__main__":
    asyncio.run(main())
