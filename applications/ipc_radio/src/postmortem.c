/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Net core: on any fatal error, stash reason/pc/lr/xpsr into a small shared
 * SRAM record (nrf/include/net_core_postmortem.h) with plain volatile stores
 * only -- no IPC service, no scheduler, no interrupts required. The app
 * core reads it back inside Network Core Monitor's RESET/FREEZE event
 * handler (nrf/samples/bluetooth/peripheral_lbs/src/postmortem_monitor.c),
 * which needs no new signal of its own -- NCM's own liveness poll already
 * distinguishes a net-core reset from a net-core freeze (wedged, never got
 * to reset), which is exactly the case a real lockup produces.
 */

#include <zephyr/kernel.h>

#include <net_core_postmortem.h>

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	(void)irq_lock();

	if (esf) {
		static uint32_t seq;
		volatile struct net_core_postmortem *pm = net_core_postmortem_get();

		pm->reason = reason;
		pm->pc = esf->basic.pc;
		pm->lr = esf->basic.lr;
		pm->xpsr = esf->basic.xpsr;
		pm->seq = ++seq;
		pm->magic = NET_CORE_POSTMORTEM_MAGIC;
		pm->valid = 1;
	}

	for (;;) {
	}
}
