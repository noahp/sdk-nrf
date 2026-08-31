/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief BLE connection TX/RX airtime estimation.
 *
 * Airtime is not measured with a timer or radio hook: it is computed from
 * the Bluetooth Core Specification packet-duration formulas (Vol 6, Part B,
 * Section 4.1), given the length of each Link Layer Data PDU and the PHY and
 * encryption state of the connection it was sent/received on. That is exact
 * for 1M and 2M PHY. For Coded PHY, the host cannot observe whether the
 * controller used S=2 or S=8 for a given PDU (the LE PHY Update Complete
 * event only reports "Coded", not the coding scheme), so this module always
 * assumes @ref BLE_CODED_DEFAULT_S. The formula constants below were cross
 * checked against the reservation constants Zephyr's own Bluetooth
 * controller uses for scheduling (PDU_DC_PAYLOAD_TIME_MIN/MAX(_CODED) in
 * zephyr/subsys/bluetooth/controller/ll_sw/pdu.h).
 *
 * The byte counts that feed the formula come from airtime_hci_wrap.c, which
 * intercepts ACL Data packets at the sdc_hci_data_put()/sdc_hci_get()
 * boundary between the host and the SoftDevice Controller. That means this
 * module only accounts for airtime spent moving actual L2CAP/ATT payload.
 * Link Layer overhead that never reaches the HCI ACL boundary - empty PDUs
 * that keep a connection event alive, LL control PDUs, retransmissions,
 * advertising - is not included. See the sample README for details.
 *
 * This module assumes a single active connection (CONFIG_BT_MAX_CONN=1, the
 * default for this sample). With multiple concurrent connections, all PDUs
 * would be accounted using the most recently updated PHY/encryption state,
 * regardless of which connection they belong to.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/logging/log.h>

#include "airtime_metrics.h"

LOG_MODULE_REGISTER(airtime_metrics, LOG_LEVEL_INF);

enum airtime_phy {
	AIRTIME_PHY_1M,
	AIRTIME_PHY_2M,
	AIRTIME_PHY_CODED,
};

/* Bit counts of the fixed fields around the PDU payload, per Core Spec
 * Vol 6, Part B, Section 2.1 (packet format) and Section 4.1 (timing).
 */
#define BITS_PREAMBLE_1M      8
#define BITS_PREAMBLE_2M      16
#define BITS_ACCESS_ADDR      32
#define BITS_PDU_HEADER       16
#define BITS_CRC              24
#define BITS_MIC              32

/* Coded PHY: FEC Block 1 (Access Address + Coding Indicator + TERM1) is
 * always sent coded at S=8, regardless of the S value used for FEC Block 2
 * (header + payload + MIC + CRC + TERM2).
 */
#define CODED_PREAMBLE_US     80
#define CODED_FEC1_BITS       37 /* AA(32) + CI(2) + TERM1(3) */
#define CODED_FEC1_CODING_S   8
#define CODED_TERM2_BITS      3
#define CODED_DEFAULT_S       8  /* See file header: not observable from the host. */

static enum airtime_phy current_tx_phy = AIRTIME_PHY_1M;
static enum airtime_phy current_rx_phy = AIRTIME_PHY_1M;
static bool current_encrypted;
static struct k_spinlock state_lock;

static uint32_t pdu_airtime_us(uint16_t payload_len, enum airtime_phy phy, bool encrypted)
{
	uint32_t mic_bits = encrypted ? BITS_MIC : 0;

	switch (phy) {
	case AIRTIME_PHY_2M: {
		uint32_t bits = BITS_PREAMBLE_2M + BITS_ACCESS_ADDR + BITS_PDU_HEADER +
				 (uint32_t)payload_len * 8 + BITS_CRC + mic_bits;
		/* 2 Mbps -> 0.5 us/bit */
		return bits / 2;
	}
	case AIRTIME_PHY_CODED: {
		uint32_t fec2_bits = BITS_PDU_HEADER + (uint32_t)payload_len * 8 + BITS_CRC +
				      mic_bits + CODED_TERM2_BITS;

		return CODED_PREAMBLE_US + (CODED_FEC1_BITS * CODED_FEC1_CODING_S) +
		       (fec2_bits * CODED_DEFAULT_S);
	}
	case AIRTIME_PHY_1M:
	default: {
		uint32_t bits = BITS_PREAMBLE_1M + BITS_ACCESS_ADDR + BITS_PDU_HEADER +
				 (uint32_t)payload_len * 8 + BITS_CRC + mic_bits;
		/* 1 Mbps -> 1 us/bit */
		return bits;
	}
	}
}

static enum airtime_phy gap_phy_to_airtime_phy(uint8_t gap_phy)
{
	switch (gap_phy) {
	case BT_GAP_LE_PHY_2M:
		return AIRTIME_PHY_2M;
	case BT_GAP_LE_PHY_CODED:
		return AIRTIME_PHY_CODED;
	case BT_GAP_LE_PHY_1M:
	default:
		return AIRTIME_PHY_1M;
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err) {
		return;
	}

	/* Every connection starts on the 1M PHY, unencrypted, per spec. */
	K_SPINLOCK(&state_lock) {
		current_tx_phy = AIRTIME_PHY_1M;
		current_rx_phy = AIRTIME_PHY_1M;
		current_encrypted = false;
	}
}

static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	ARG_UNUSED(conn);

	enum airtime_phy tx_phy = gap_phy_to_airtime_phy(param->tx_phy);
	enum airtime_phy rx_phy = gap_phy_to_airtime_phy(param->rx_phy);

	K_SPINLOCK(&state_lock) {
		current_tx_phy = tx_phy;
		current_rx_phy = rx_phy;
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(conn);

	if (err) {
		return;
	}

	bool encrypted = level >= BT_SECURITY_L2;

	K_SPINLOCK(&state_lock) {
		current_encrypted = encrypted;
	}
}

BT_CONN_CB_DEFINE(airtime_conn_callbacks) = {
	.connected = connected,
	.le_phy_updated = le_phy_updated,
	.security_changed = security_changed,
};

struct airtime_totals {
	uint64_t bytes;
	uint64_t packets;
	uint64_t us;
};

static struct airtime_totals tx_totals;
static struct airtime_totals rx_totals;
static struct k_spinlock stats_lock;

void airtime_on_tx_acl_pdu(uint16_t payload_len)
{
	enum airtime_phy phy;
	bool encrypted;

	K_SPINLOCK(&state_lock) {
		phy = current_tx_phy;
		encrypted = current_encrypted;
	}

	uint32_t us = pdu_airtime_us(payload_len, phy, encrypted);

	K_SPINLOCK(&stats_lock) {
		tx_totals.bytes += payload_len;
		tx_totals.packets += 1;
		tx_totals.us += us;
	}
}

void airtime_on_rx_acl_pdu(uint16_t payload_len)
{
	enum airtime_phy phy;
	bool encrypted;

	K_SPINLOCK(&state_lock) {
		phy = current_rx_phy;
		encrypted = current_encrypted;
	}

	uint32_t us = pdu_airtime_us(payload_len, phy, encrypted);

	K_SPINLOCK(&stats_lock) {
		rx_totals.bytes += payload_len;
		rx_totals.packets += 1;
		rx_totals.us += us;
	}
}

#define REPORT_INTERVAL_S 10
#define REPORT_INTERVAL_US ((uint64_t)REPORT_INTERVAL_S * USEC_PER_SEC)

static void report_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(report_work, report_work_handler);

static void report_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct airtime_totals tx;
	struct airtime_totals rx;

	K_SPINLOCK(&stats_lock) {
		tx = tx_totals;
		rx = rx_totals;
		memset(&tx_totals, 0, sizeof(tx_totals));
		memset(&rx_totals, 0, sizeof(rx_totals));
	}

	if (tx.packets != 0 || rx.packets != 0) {
		LOG_INF("Airtime/%ds: TX %llu B, %llu pdu, %llu us (%llu.%01llu%%) | "
			"RX %llu B, %llu pdu, %llu us (%llu.%01llu%%)",
			REPORT_INTERVAL_S,
			tx.bytes, tx.packets, tx.us,
			(tx.us * 1000 / REPORT_INTERVAL_US) / 10,
			(tx.us * 1000 / REPORT_INTERVAL_US) % 10,
			rx.bytes, rx.packets, rx.us,
			(rx.us * 1000 / REPORT_INTERVAL_US) / 10,
			(rx.us * 1000 / REPORT_INTERVAL_US) % 10);
	}

	k_work_schedule(&report_work, K_SECONDS(REPORT_INTERVAL_S));
}

static int airtime_metrics_init(void)
{
	k_work_schedule(&report_work, K_SECONDS(REPORT_INTERVAL_S));

	return 0;
}

SYS_INIT(airtime_metrics_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
