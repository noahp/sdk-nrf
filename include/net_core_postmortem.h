/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * PoC: a small cross-core "postmortem" record for a network-core fault.
 *
 * It lives in a few bytes carved out of the tail of the app/net shared IPC
 * memory region (see the devicetree overlays applied to both core images —
 * overlay-vs-fatal-error-poc.overlay on the net core, overlay-postmortem-poc.overlay
 * on the app core). Written and read with plain volatile memory stores/loads
 * only: no IPC service, no scheduler, no interrupts required. That's the
 * point — it has to survive exactly the case where the live HCI
 * vendor-event relay can't (see overlay-vs-fatal-error-poc.conf's comments
 * on the RPMSG backend needing cooperative scheduling to deliver anything).
 */

#ifndef NET_CORE_POSTMORTEM_H_
#define NET_CORE_POSTMORTEM_H_

#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_CORE_POSTMORTEM_MAGIC 0x504D4657U /* "PMFW" */

struct net_core_postmortem {
	uint32_t magic;
	uint32_t valid;
	uint32_t reason;
	uint32_t pc;
	uint32_t lr;
	uint32_t xpsr;
	uint32_t seq;
	uint32_t reserved;
};

#define NET_CORE_POSTMORTEM_ADDR DT_REG_ADDR(DT_NODELABEL(net_core_postmortem))
#define NET_CORE_POSTMORTEM_SIZE DT_REG_SIZE(DT_NODELABEL(net_core_postmortem))

BUILD_ASSERT(sizeof(struct net_core_postmortem) <= NET_CORE_POSTMORTEM_SIZE,
	     "net_core_postmortem no longer fits in its reserved memory carve-out");

static inline volatile struct net_core_postmortem *net_core_postmortem_get(void)
{
	return (volatile struct net_core_postmortem *)NET_CORE_POSTMORTEM_ADDR;
}

#ifdef __cplusplus
}
#endif

#endif /* NET_CORE_POSTMORTEM_H_ */
