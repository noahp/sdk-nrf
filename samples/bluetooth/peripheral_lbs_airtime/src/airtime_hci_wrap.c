/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Linker-level tap into the SoftDevice Controller HCI boundary.
 *
 * sdc_hci_data_put() and sdc_hci_get() (declared in nrfxlib's sdc_hci.h) are
 * the exact functions the Bluetooth controller glue
 * (nrf/subsys/bluetooth/controller/hci_driver.c and hci_internal.c) use to
 * hand ACL Data packets to, and retrieve them from, the SoftDevice
 * Controller. Wrapping them with the linker's --wrap gives byte-accurate
 * TX/RX ACL traffic counts without touching the application, the Bluetooth
 * host, or the controller glue code. This is Nordic/SDC specific (it relies
 * on those two symbols existing), but nothing here is specific to
 * peripheral_lbs - it works unmodified in any SDC-based application. See
 * CMakeLists.txt for the -Wl,--wrap= flags that make this active.
 *
 * Only ACL Data packets are covered. Isochronous data uses a separate
 * sdc_hci_iso_data_put() on the TX side and would need its own wrap.
 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/hci_types.h>

#include <sdc_hci.h>

#include "airtime_metrics.h"

int32_t __real_sdc_hci_data_put(uint8_t const *p_data_in);
int32_t __real_sdc_hci_get(uint8_t *p_packet_out, uint8_t *p_msg_type_out);

int32_t __wrap_sdc_hci_data_put(uint8_t const *p_data_in)
{
	int32_t err = __real_sdc_hci_data_put(p_data_in);

	if (err == 0) {
		const struct bt_hci_acl_hdr *hdr = (const void *)p_data_in;

		airtime_on_tx_acl_pdu(sys_le16_to_cpu(hdr->len));
	}

	return err;
}

int32_t __wrap_sdc_hci_get(uint8_t *p_packet_out, uint8_t *p_msg_type_out)
{
	int32_t err = __real_sdc_hci_get(p_packet_out, p_msg_type_out);

	if (err == 0 && *p_msg_type_out == SDC_HCI_MSG_TYPE_DATA) {
		const struct bt_hci_acl_hdr *hdr = (const void *)p_packet_out;

		airtime_on_rx_acl_pdu(sys_le16_to_cpu(hdr->len));
	}

	return err;
}
