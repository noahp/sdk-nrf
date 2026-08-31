#
# Copyright (c) 2026 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/airtime_metrics.c
  src/airtime_hci_wrap.c
)

# Redirect the SoftDevice Controller's HCI ACL Data entry points through our
# wrap functions (airtime_hci_wrap.c). See that file for why these two
# symbols are the right interception point.
target_link_options(app PUBLIC
  -Wl,--wrap=sdc_hci_data_put
  -Wl,--wrap=sdc_hci_get
)
# NORDIC SDK APP END
