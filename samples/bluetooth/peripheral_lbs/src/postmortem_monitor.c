/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * PoC: override Network Core Monitor's event handler to read out the net
 * core's retained-memory postmortem record (nrf/include/net_core_postmortem.h)
 * whenever NCM reports the net core reset or froze. No polling of our own,
 * no extra signal needed — NCM already provides the trigger.
 */

#include <stdint.h>

#include <net_core_monitor.h>
#include <net_core_postmortem.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_core_postmortem, LOG_LEVEL_INF);

void ncm_net_core_event_handler(enum ncm_event_type event, uint32_t reset_reas)
{
	volatile struct net_core_postmortem *pm = net_core_postmortem_get();

	switch (event) {
	case NCM_EVT_NET_CORE_RESET:
		LOG_WRN("Network core reset (RESETREAS=0x%08x)", reset_reas);
		break;
	case NCM_EVT_NET_CORE_FREEZE:
		LOG_WRN("Network core is not responding (frozen, no reset seen)");
		break;
	}

	if (pm->magic == NET_CORE_POSTMORTEM_MAGIC && pm->valid) {
		LOG_ERR("Network core fault record: reason=%u pc=0x%08x lr=0x%08x "
			"xpsr=0x%08x seq=%u",
			pm->reason, pm->pc, pm->lr, pm->xpsr, pm->seq);
		pm->valid = 0;
	} else {
		LOG_INF("No postmortem record present (magic=0x%08x valid=%u)", pm->magic,
			pm->valid);
	}
}
