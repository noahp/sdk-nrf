/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/lbs.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>
#include <hw_id.h>

#include "memfault_event.h"

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)


#define RUN_STATUS_LED          DK_LED1
#define CON_STATUS_LED          DK_LED2
#define RUN_LED_BLINK_INTERVAL  1000

#define USER_LED                DK_LED3

#define USER_BUTTON             DK_BTN1_MSK

/* Simulated battery voltage range, in millivolts. There is no fuel gauge on
 * the DK, so this ramps up and down over time to produce varying data for
 * the Memfault event relay to forward.
 */
#define MEMFAULT_EVENT_BATT_MV_MIN       3300
#define MEMFAULT_EVENT_BATT_MV_MAX       4200
#define MEMFAULT_EVENT_BATT_CYCLE_SEC    120
#define MEMFAULT_EVENT_NOTIFY_INTERVAL   K_SECONDS(CONFIG_MEMFAULT_EVENT_NOTIFY_INTERVAL_SECONDS)

static bool app_button_state;
static uint32_t app_button_presses;
static struct k_work adv_work;
static struct k_work_delayable memfault_event_work;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};

static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	if (err) {
		printk("MTU exchange failed (err %u)\n", err);
	} else {
		printk("MTU exchange successful, MTU: %u\n", bt_gatt_get_mtu(conn));
	}
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
	.func = mtu_exchange_cb,
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	int mtu_err;

	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}

	printk("Connected\n");

	dk_set_led_on(CON_STATUS_LED);
	memfault_event_svc_set_conn(conn);

	/* Requesting a larger MTU reduces the number of fragments the
	 * Memfault event needs to be split into (see memfault_event.c), but
	 * is not required: fragmentation falls back to the 23-byte default
	 * if this fails or the peer only agrees to a small MTU.
	 */
	mtu_err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
	if (mtu_err) {
		printk("MTU exchange request failed (err %d)\n", mtu_err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

	dk_set_led_off(CON_STATUS_LED);
	memfault_event_svc_set_conn(NULL);
}

static void recycled_cb(void)
{
	printk("Connection object available from previous conn. Disconnect is complete!\n");
	advertising_start();
}

#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d %s\n", addr, level, err,
		       bt_security_err_to_str(err));
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
#ifdef CONFIG_BT_LBS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_LBS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d %s\n", addr, reason,
	       bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif

static void app_led_cb(bool led_state)
{
	dk_set_led(USER_LED, led_state);
}

static bool app_button_cb(void)
{
	return app_button_state;
}

static struct bt_lbs_cb lbs_callbacs = {
	.led_cb    = app_led_cb,
	.button_cb = app_button_cb,
};

/* Simulate a battery voltage reading. Real applications should replace this
 * with a call into a fuel gauge or ADC driver.
 */
static int32_t simulated_batt_mv_get(uint32_t uptime_s)
{
	uint32_t half_cycle_s = MEMFAULT_EVENT_BATT_CYCLE_SEC / 2;
	uint32_t phase_s = uptime_s % MEMFAULT_EVENT_BATT_CYCLE_SEC;
	uint32_t span_mv = MEMFAULT_EVENT_BATT_MV_MAX - MEMFAULT_EVENT_BATT_MV_MIN;

	if (phase_s < half_cycle_s) {
		return MEMFAULT_EVENT_BATT_MV_MIN + (span_mv * phase_s / half_cycle_s);
	}

	return MEMFAULT_EVENT_BATT_MV_MAX - (span_mv * (phase_s - half_cycle_s) / half_cycle_s);
}

static void memfault_event_notify_current_state(void)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / MSEC_PER_SEC);
	int err;

	err = memfault_event_notify(uptime_s, app_button_state, app_button_presses,
				     simulated_batt_mv_get(uptime_s));
	if (err && err != -EACCES) {
		printk("Failed to notify Memfault event (err %d)\n", err);
	}
}

static void memfault_event_work_handler(struct k_work *work)
{
	memfault_event_notify_current_state();

	k_work_schedule(&memfault_event_work, MEMFAULT_EVENT_NOTIFY_INTERVAL);
}

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON) {
		uint32_t user_button_state = button_state & USER_BUTTON;

		bt_lbs_send_button_state(user_button_state);
		app_button_state = user_button_state ? true : false;

		if (app_button_state) {
			app_button_presses++;
		}

		memfault_event_notify_current_state();
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}

/* Populate the Memfault device identity embedded in every CBOR event, as if
 * it was reported by a downstream device that owns its own identity.
 */
static void init_memfault_device_info(void)
{
	char serial[40];
	int err;

	err = hw_id_get(serial, sizeof(serial));
	if (err) {
		printk("Failed to get HW ID (err %d), using a placeholder device serial\n", err);
		strncpy(serial, "UNKNOWN", sizeof(serial) - 1);
	}

	memfault_event_set_device_info(serial, CONFIG_MEMFAULT_SOFTWARE_TYPE,
					CONFIG_MEMFAULT_SOFTWARE_VERSION,
					CONFIG_MEMFAULT_HARDWARE_VERSION);
}

int main(void)
{
	int blink_status = 0;
	int err;

	printk("Starting Bluetooth Peripheral LBS sample\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_BT_LBS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return 0;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return 0;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	/* Needs the identity address set up by settings_load() (or the
	 * default identity created in bt_enable() if settings are disabled).
	 */
	init_memfault_device_info();

	err = bt_lbs_init(&lbs_callbacs);
	if (err) {
		printk("Failed to init LBS (err:%d)\n", err);
		return 0;
	}

	k_work_init(&adv_work, adv_work_handler);
	advertising_start();

	k_work_init_delayable(&memfault_event_work, memfault_event_work_handler);
	k_work_schedule(&memfault_event_work, MEMFAULT_EVENT_NOTIFY_INTERVAL);

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
