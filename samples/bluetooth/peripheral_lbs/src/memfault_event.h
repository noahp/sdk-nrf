/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MEMFAULT_EVENT_H_
#define MEMFAULT_EVENT_H_

/**@file
 * @defgroup bt_memfault_event Memfault Event Service
 * @{
 * @brief GATT service that streams device metrics as a CBOR payload.
 *
 * The characteristic exposed by this service is intended to be relayed by a
 * host-side application to the Memfault "Ingress" events API
 * (https://api-docs.memfault.com/#7267d576-ee78-4d1c-b7a3-8fc4dd82ce04).
 * See :file:`scripts/memfault_ble_relay.py` for a reference relay
 * implementation.
 *
 * The Memfault device identity fields (device serial, software type,
 * software version, and hardware version) are embedded in every CBOR
 * payload, simulating a self-describing custom data package coming from a
 * downstream device that the host has no other knowledge of.
 *
 * Since the encoded payload does not reliably fit in a single notification
 * (not every central negotiates a large ATT MTU), each event is split into
 * fragments that individually fit the connection's negotiated MTU. See
 * :file:`scripts/memfault_ble_relay.py` for the reassembly logic.
 */

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

/** @brief Memfault Event Service UUID. */
#define BT_UUID_MEMFAULT_EVENT_SVC_VAL \
	BT_UUID_128_ENCODE(0x00001526, 0x1212, 0xefde, 0x1523, 0x785feabcd123)

/** @brief Memfault Event Data Characteristic UUID. */
#define BT_UUID_MEMFAULT_EVENT_DATA_VAL \
	BT_UUID_128_ENCODE(0x00001527, 0x1212, 0xefde, 0x1523, 0x785feabcd123)

#define BT_UUID_MEMFAULT_EVENT_SVC  BT_UUID_DECLARE_128(BT_UUID_MEMFAULT_EVENT_SVC_VAL)
#define BT_UUID_MEMFAULT_EVENT_DATA BT_UUID_DECLARE_128(BT_UUID_MEMFAULT_EVENT_DATA_VAL)

/** @brief Set the Memfault device identity embedded in every notified event.
 *
 * Each string is copied internally, so the arguments do not need to remain
 * valid after this call returns. Call this once, before the first call to
 * @ref memfault_event_notify.
 *
 * @param[in] device_serial     Device serial, for example derived from a
 *                               hardware identifier.
 * @param[in] software_type     Software type, for example the application
 *                               name.
 * @param[in] software_version  Software version.
 * @param[in] hardware_version  Hardware version, for example the board
 *                               target.
 */
void memfault_event_set_device_info(const char *device_serial, const char *software_type,
				     const char *software_version,
				     const char *hardware_version);

/** @brief Set the connection that Memfault events are sent to.
 *
 * Call this with the active connection when a central connects, and with
 * NULL when it disconnects. This sample only tracks a single connection at
 * a time.
 *
 * @param[in] conn The active connection, or NULL if none.
 */
void memfault_event_svc_set_conn(struct bt_conn *conn);

/** @brief Encode the current metrics as CBOR and notify subscribed peers.
 *
 * The resulting CBOR payload is a map with the following keys:
 *
 * - "device_serial" (tstr): Device serial, as set by
 *   @ref memfault_event_set_device_info.
 * - "software_type" (tstr): Software type, as set by
 *   @ref memfault_event_set_device_info.
 * - "software_version" (tstr): Software version, as set by
 *   @ref memfault_event_set_device_info.
 * - "hardware_version" (tstr): Hardware version, as set by
 *   @ref memfault_event_set_device_info.
 * - "uptime_s" (uint): Seconds since boot.
 * - "button" (bool): Current state of the user button.
 * - "button_presses" (uint): Number of button presses since boot.
 * - "batt_mv" (int): Simulated battery voltage, in millivolts.
 *
 * The payload is split into one or more notifications; see this file's
 * top-level comment for the fragmentation format.
 *
 * @param[in] uptime_s        Seconds since boot.
 * @param[in] button_state    Current state of the user button.
 * @param[in] button_presses  Number of button presses since boot.
 * @param[in] batt_mv         Simulated battery voltage, in millivolts.
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int memfault_event_notify(uint32_t uptime_s, bool button_state, uint32_t button_presses,
			   int32_t batt_mv);

/**
 * @}
 */

#endif /* MEMFAULT_EVENT_H_ */
