.. _peripheral_lbs_airtime:

Bluetooth: LED Button Service with airtime metrics
###################################################

.. contents::
   :local:
   :depth: 2

This sample is a copy of :ref:`peripheral_lbs` (unmodified) with a
self-contained module added that estimates and periodically logs the BLE
radio airtime spent transmitting and receiving application data. It targets
the nRF54L15 (Bluetooth LE via the SoftDevice Controller), but the technique
does not depend on peripheral_lbs itself.

Overview
********

There is no hardware or MPSL API that directly measures per-packet TX/RX
airtime on nRF SoCs. Instead, this sample computes it, using two pieces:

1. **The Bluetooth Core Specification packet-duration formulas**
   (Vol 6, Part B, Section 4.1), which give the exact on-air time of a Link
   Layer Data PDU from its payload length, the PHY (1M / 2M / Coded), and
   whether the link is encrypted (adds a 4-byte MIC). See
   :file:`src/airtime_metrics.c` for the formulas and the reasoning behind
   them, including how the Coded PHY constants were cross-checked against
   the reservation constants Zephyr's own Bluetooth controller uses
   internally for scheduling.

2. **A linker-level tap on the HCI ACL Data boundary**, so the byte counts
   that feed the formula are collected automatically, without any change to
   the application. :file:`src/airtime_hci_wrap.c` uses
   ``-Wl,--wrap=sdc_hci_data_put`` and ``-Wl,--wrap=sdc_hci_get`` (see
   :file:`CMakeLists.txt`) to intercept every ACL Data packet exchanged
   between the Zephyr Bluetooth host and the SoftDevice Controller. Each such
   packet corresponds 1:1 to one Link Layer Data PDU, so its length is
   exactly what the formula needs.

Because of where the tap sits, :file:`src/main.c` is byte-for-byte the same
file as in :ref:`peripheral_lbs`: the LED/button application has no idea the
airtime module exists. That is the point - a customer application (or, for
that matter, the Memfault SDK) does not need to instrument its own TX calls
to get this data.

What this does *not* measure
*****************************

The wrap only sees ACL Data packets, i.e. actual L2CAP/ATT application
payload. It does **not** account for:

* Empty PDUs that keep a connection event alive when there is no data to
  send - these are the majority of a typical low-duty-cycle peripheral's
  airtime and are invisible above HCI.
* LL control PDUs (connection parameter updates, PHY/data length updates,
  encryption setup, and so on).
* Retransmissions.
* Advertising and scanning.

So the numbers this sample prints are "airtime spent moving the data my
application asked for," not "the connection's total radio duty cycle."
Getting the latter would require either the MPSL radio notification API
(``mpsl_radio_notification_cfg_set()``) to time whole connection events, or a
current probe on the board.

Coded PHY caveat
****************

The host cannot observe whether the controller used S=2 or S=8 coding for a
given Coded PHY packet - the LE PHY Update Complete event only reports
"Coded", not the coding scheme. This sample always assumes S=8
(``CODED_DEFAULT_S`` in :file:`src/airtime_metrics.c`). 1M and 2M PHY
airtime is exact.

Multiple connections caveat
****************************

The module tracks one set of PHY/encryption state, matching this sample's
default of ``CONFIG_BT_MAX_CONN=1``. With more than one simultaneous
connection, all PDUs would be (incorrectly) accounted using whichever
connection's state was updated most recently. See the comment at the top of
:file:`src/airtime_metrics.c`.

Requirements
************

The sample supports the following development kit:

* nRF54L15 DK (``nrf54l15dk/nrf54l15/cpuapp``)

Building and running
*********************
.. |sample path| replace:: :file:`samples/bluetooth/peripheral_lbs_airtime`

.. include:: /includes/build_and_run.txt

Testing
=======

1. Connect the DK to a computer with a serial terminal (see
   :ref:`test_and_optimize`).
#. Reset the kit.
#. Connect with a phone or `nRF Connect for Desktop`_ and interact with the
   LED Button service as described in :ref:`peripheral_lbs`.
#. Every 10 seconds, observe a log line similar to::

      Airtime/10s: TX 1520 B, 12 pdu, 13120 us (0.1%) | RX 640 B, 8 pdu, 6720 us (0.0%)

   TX/RX byte and packet counts are for that 10-second window; the airtime
   and percentage are the module's estimate of how much of that window was
   spent transmitting or receiving the corresponding application data.

Dependencies
*************

This sample uses the same dependencies as :ref:`peripheral_lbs`, plus:

* `nrfxlib`_ ``sdc_hci.h`` (from :ref:`nrfxlib:softdevice_controller`) for the
  wrapped symbols' declarations and the ``SDC_HCI_MSG_TYPE_DATA`` constant.
