/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <string.h>
#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include <zcbor_encode.h>

#include "memfault_event.h"

/* Large enough for the map header, the 4 device info strings (bounded by
 * MEMFAULT_EVENT_DEVICE_INFO_STR_LEN each), and the 4 metric key/value pairs.
 */
#define MEMFAULT_EVENT_CBOR_BUF_SIZE 256

#define MEMFAULT_EVENT_DEVICE_INFO_STR_LEN 40

/* Not every central negotiates an ATT MTU large enough to fit the whole
 * event in a single notification (some stay at the 23-byte default, or
 * cap out lower than the encoded payload regardless). Rather than rely on
 * a large MTU, the event is split into fragments that each fit in whatever
 * MTU is in use, and reassembled on the host side. Each fragment is
 * prefixed with a 1-byte header: bits 0-6 are the 0-based fragment index,
 * and bit 7 is set on the last fragment.
 */
#define MEMFAULT_EVENT_FRAG_HEADER_LEN 1
#define MEMFAULT_EVENT_FRAG_LAST_BIT   0x80
#define MEMFAULT_EVENT_FRAG_INDEX_MAX  (MEMFAULT_EVENT_FRAG_LAST_BIT - 1)
#define MEMFAULT_EVENT_ATT_HEADER_LEN  3
#define MEMFAULT_EVENT_MIN_ATT_MTU     23

#define MEMFAULT_EVENT_NOTIFY_RETRY_COUNT 3
#define MEMFAULT_EVENT_NOTIFY_RETRY_DELAY K_MSEC(10)

static bool notify_enabled;
static struct bt_conn *active_conn;

static char device_serial[MEMFAULT_EVENT_DEVICE_INFO_STR_LEN];
static char software_type[MEMFAULT_EVENT_DEVICE_INFO_STR_LEN];
static char software_version[MEMFAULT_EVENT_DEVICE_INFO_STR_LEN];
static char hardware_version[MEMFAULT_EVENT_DEVICE_INFO_STR_LEN];

static void mfld_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Memfault event notifications %s\n", notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(memfault_event_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_MEMFAULT_EVENT_SVC),
	BT_GATT_CHARACTERISTIC(BT_UUID_MEMFAULT_EVENT_DATA, BT_GATT_CHRC_NOTIFY,
				BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(mfld_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void memfault_event_set_device_info(const char *serial, const char *sw_type,
				     const char *sw_version, const char *hw_version)
{
	strncpy(device_serial, serial, sizeof(device_serial) - 1);
	strncpy(software_type, sw_type, sizeof(software_type) - 1);
	strncpy(software_version, sw_version, sizeof(software_version) - 1);
	strncpy(hardware_version, hw_version, sizeof(hardware_version) - 1);
}

void memfault_event_svc_set_conn(struct bt_conn *conn)
{
	active_conn = conn;
}

static int send_fragments(struct bt_conn *conn, const uint8_t *data, size_t len)
{
	/* MEMFAULT_EVENT_CBOR_BUF_SIZE comfortably fits in one fragment's
	 * data portion even at the largest realistic negotiated MTU, so
	 * sizing the scratch buffer off of it covers every chunk size below.
	 */
	uint8_t frag_buf[MEMFAULT_EVENT_FRAG_HEADER_LEN + MEMFAULT_EVENT_CBOR_BUF_SIZE];
	uint16_t att_mtu = conn ? bt_gatt_get_mtu(conn) : MEMFAULT_EVENT_MIN_ATT_MTU;
	size_t max_chunk_len;
	size_t num_frags;

	if (att_mtu < MEMFAULT_EVENT_MIN_ATT_MTU) {
		att_mtu = MEMFAULT_EVENT_MIN_ATT_MTU;
	}

	max_chunk_len = att_mtu - MEMFAULT_EVENT_ATT_HEADER_LEN - MEMFAULT_EVENT_FRAG_HEADER_LEN;
	num_frags = DIV_ROUND_UP(len, max_chunk_len);

	if (num_frags > MEMFAULT_EVENT_FRAG_INDEX_MAX + 1) {
		printk("Memfault event too large to fragment (%zu fragments needed)\n",
		       num_frags);
		return -EMSGSIZE;
	}

	for (size_t i = 0; i < num_frags; i++) {
		size_t offset = i * max_chunk_len;
		size_t chunk_len = MIN(max_chunk_len, len - offset);
		int err;

		frag_buf[0] = i;
		if (i == num_frags - 1) {
			frag_buf[0] |= MEMFAULT_EVENT_FRAG_LAST_BIT;
		}
		memcpy(&frag_buf[MEMFAULT_EVENT_FRAG_HEADER_LEN], &data[offset], chunk_len);

		for (int attempt = 0; attempt < MEMFAULT_EVENT_NOTIFY_RETRY_COUNT; attempt++) {
			err = bt_gatt_notify(conn, &memfault_event_svc.attrs[2], frag_buf,
					     chunk_len + MEMFAULT_EVENT_FRAG_HEADER_LEN);
			if (err != -ENOMEM) {
				break;
			}
			k_sleep(MEMFAULT_EVENT_NOTIFY_RETRY_DELAY);
		}

		if (err) {
			printk("Failed to notify Memfault event fragment %zu/%zu (err %d)\n",
			       i + 1, num_frags, err);
			return err;
		}
	}

	return 0;
}

int memfault_event_notify(uint32_t uptime_s, bool button_state, uint32_t button_presses,
			   int32_t batt_mv)
{
	uint8_t cbor_buf[MEMFAULT_EVENT_CBOR_BUF_SIZE];
	ZCBOR_STATE_E(state, 0, cbor_buf, sizeof(cbor_buf), 1);
	bool ok;

	if (!notify_enabled || !active_conn) {
		return -EACCES;
	}

	ok = zcbor_map_start_encode(state, 8) &&
	     zcbor_tstr_put_lit(state, "device_serial") &&
	     zcbor_tstr_put_term(state, device_serial, sizeof(device_serial)) &&
	     zcbor_tstr_put_lit(state, "software_type") &&
	     zcbor_tstr_put_term(state, software_type, sizeof(software_type)) &&
	     zcbor_tstr_put_lit(state, "software_version") &&
	     zcbor_tstr_put_term(state, software_version, sizeof(software_version)) &&
	     zcbor_tstr_put_lit(state, "hardware_version") &&
	     zcbor_tstr_put_term(state, hardware_version, sizeof(hardware_version)) &&
	     zcbor_tstr_put_lit(state, "uptime_s") && zcbor_uint32_put(state, uptime_s) &&
	     zcbor_tstr_put_lit(state, "button") && zcbor_bool_put(state, button_state) &&
	     zcbor_tstr_put_lit(state, "button_presses") &&
	     zcbor_uint32_put(state, button_presses) &&
	     zcbor_tstr_put_lit(state, "batt_mv") && zcbor_int32_put(state, batt_mv) &&
	     zcbor_map_end_encode(state, 8);

	if (!ok) {
		printk("Failed to CBOR-encode Memfault event\n");
		return -EINVAL;
	}

	size_t len = state->payload - cbor_buf;

	printk("Sending Memfault event to peer: uptime_s=%u button=%u button_presses=%u "
	       "batt_mv=%d (%zu bytes)\n",
	       uptime_s, button_state, button_presses, batt_mv, len);

	return send_fragments(active_conn, cbor_buf, len);
}
