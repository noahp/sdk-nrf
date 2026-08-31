/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef AIRTIME_METRICS_H_
#define AIRTIME_METRICS_H_

#include <stdint.h>

/**
 * @brief Record that an HCI ACL Data PDU was handed to the controller for
 *        transmission.
 *
 * Called from the HCI wrap layer (airtime_hci_wrap.c) for every ACL Data
 * packet that crosses the sdc_hci_data_put() boundary. Each such packet
 * corresponds 1:1 to one Link Layer Data PDU, so payload_len is exactly the
 * PDU payload length used in the airtime formula.
 *
 * @param payload_len HCI ACL Data payload length, in bytes.
 */
void airtime_on_tx_acl_pdu(uint16_t payload_len);

/**
 * @brief Record that an HCI ACL Data PDU was received from the controller.
 *
 * Called from the HCI wrap layer for every ACL Data packet retrieved via
 * sdc_hci_get(). See @ref airtime_on_tx_acl_pdu for the packet/PDU
 * correspondence.
 *
 * @param payload_len HCI ACL Data payload length, in bytes.
 */
void airtime_on_rx_acl_pdu(uint16_t payload_len);

#endif /* AIRTIME_METRICS_H_ */
