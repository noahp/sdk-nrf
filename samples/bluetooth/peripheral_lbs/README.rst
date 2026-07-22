.. _peripheral_lbs:

Bluetooth: Peripheral LBS
#########################

.. contents::
   :local:
   :depth: 2

The peripheral LBS sample demonstrates how to use the :ref:`lbs_readme`.

Requirements
************

The sample supports the following development kits:

.. table-from-sample-yaml::

.. include:: /includes/tfm.txt

The sample also requires a smartphone or tablet running a compatible mobile application.
The `Testing`_ instructions refer to `nRF Connect for Mobile`_ and `nRF Blinky`_, but you can also use other similar applications, such as `nRF Toolbox`_.

.. note::
   |thingy53_sample_note|

Overview
********

You can use the sample to transmit the button state from your development kit to another device.

.. tabs::

   .. group-tab:: nRF52 and nRF53 DKs

      When connected, the sample sends the state of **Button 1** on the development kit to the connected device, such as a phone or tablet.
      The mobile application on the device can display the received button state and control the state of **LED 3** on the development kit.

   .. group-tab:: nRF54 DKs

      When connected, the sample sends the state of **Button 0** on the development kit to the connected device, such as a phone or tablet.
      The mobile application on the device can display the received button state and control the state of **LED 2** on the development kit.

You can also use this sample to control the color of the RGB LED on the nRF52840 Dongle or Thingy:53.

Memfault event relay
=====================

The sample also demonstrates how to stream device metrics from a Bluetooth LE peripheral to `Memfault <https://memfault.com>`_ using the Memfault `Ingress events API <https://api-docs.memfault.com/#7267d576-ee78-4d1c-b7a3-8fc4dd82ce04>`_.

The device exposes a **Memfault Event Service** with a single notifiable **Memfault Event Data** characteristic.
Every :kconfig:option:`CONFIG_MEMFAULT_EVENT_NOTIFY_INTERVAL_SECONDS` seconds, and immediately whenever the user button is pressed or released, the sample encodes the following data as a `CBOR <https://cbor.io/>`_ map and sends it as a notification:

* ``device_serial`` - Device serial, derived from the device's Bluetooth LE MAC address.
* ``software_type`` - Software type, set by :kconfig:option:`CONFIG_MEMFAULT_SOFTWARE_TYPE`.
* ``software_version`` - Software version, set by :kconfig:option:`CONFIG_MEMFAULT_SOFTWARE_VERSION`.
* ``hardware_version`` - Hardware version, set by :kconfig:option:`CONFIG_MEMFAULT_HARDWARE_VERSION` (defaults to the board target).
* ``uptime_s`` - Seconds since boot.
* ``button`` - Current state of the user button.
* ``button_presses`` - Number of button presses since boot.
* ``batt_mv`` - Simulated battery voltage, in millivolts (there is no fuel gauge on the DK, so this is a synthetic value included to demonstrate a metric that changes over time).

The device identity fields (``device_serial``, ``software_type``, ``software_version``, and ``hardware_version``) are included in every payload to simulate a self-describing custom data package coming from a downstream device whose identity is otherwise unknown to the host.

Including them makes the CBOR map large enough that it does not reliably fit in a single notification: a GATT notification cannot exceed the connection's negotiated ATT MTU, and plenty of BLE centrals stay at, or cap out not far above, the 23-byte default regardless of what the sample requests.
To work around this, the sample raises :kconfig:option:`CONFIG_BT_L2CAP_TX_MTU` to ``247`` and asks the central for a larger MTU on connect, but does not depend on that request succeeding: every event is split into as many notifications as the negotiated MTU requires, each prefixed with a 1-byte header (bits 0-6: 0-based fragment index, bit 7: set on the last fragment).

The :file:`scripts/memfault_ble_relay.py` host-side Python script connects to the device over Bluetooth LE, subscribes to these notifications, reassembles the fragments and decodes the CBOR payload, and forwards each event as a ``heartbeat`` event to the Memfault events API, reading the device identity straight out of the payload.
See `Relaying events to Memfault`_ for usage instructions.

User interface
**************

The user interface of the sample depends on the hardware platform you are using.

.. tabs::

   .. group-tab:: nRF52 and nRF53 DKs

      LED 1:
         Blinks when the main loop is running (that is, the device is advertising) with a period of two seconds, duty cycle 50%.

      LED 2:
         Lit when the development kit is connected.

      LED 3:
         Lit when the development kit is controlled remotely from the connected device.

      Button 1:
         Send a notification with the button state: "pressed" or "released".

   .. group-tab:: nRF54 DKs

      LED 0:
         Blinks when the main loop is running (that is, the device is advertising) with a period of two seconds, duty cycle 50%.

      LED 1:
         Lit when the development kit is connected.

      LED 2:
         Lit when the development kit is controlled remotely from the connected device.

      Button 0:
         Send a notification with the button state: "pressed" or "released".

   .. group-tab:: Thingy:53

      RGB LED:
         The RGB LED channels are used independently to display the following information:

         * Red - If the main loop is running (that is, the device is advertising).
           The LED blinks with a period of two seconds, duty cycle 50%.
         * Green - If the device is connected.
         * Blue - If user set the LED using Nordic LED Button Service.

         For example, if Thingy:53 is connected over Bluetooth, the LED color toggles between green and yellow.
         The green LED channel is kept on and the red LED channel is blinking.

      Button 1:
         Send a notification with the button state: "pressed" or "released".

   .. group-tab:: nRF52840 Dongle

      Green LED:
         Blinks, toggling on/off every second, when the main loop is running and the device is advertising.

      RGB LED:
         The RGB LED channels are used independently to display the following information:

         * Red - If Dongle is connected.
         * Green - If user set the LED using Nordic LED Button Service.

      Button 1:
         Send a notification with the button state: "pressed" or "released".

Building and running
********************

.. |sample path| replace:: :file:`samples/bluetooth/peripheral_lbs`

.. include:: /includes/build_and_run_ns.txt

.. include:: /includes/nRF54H20_erase_UICR.txt

Minimal build
=============

You can build the sample with a minimum configuration as a demonstration of how to reduce code size and RAM usage, using the ``-DFILE_SUFFIX=minimal`` flag in your build.

See :ref:`cmake_options` for instructions on how to add this option to your build.
For example, when building on the command line, you can add the option as follows:

.. code-block:: console

   west build samples/bluetooth/peripheral_lbs -- -DFILE_SUFFIX=minimal

.. note::
   The minimal build's reduced Bluetooth buffer configuration does not raise the ATT MTU, so the Memfault event (see `Memfault event relay`_) is split into more, smaller fragments in this build than in the default configuration. This does not prevent it from working.

.. _peripheral_lbs_testing:

Testing
=======

After programming the sample to your dongle or development kit, one of the LEDs starts blinking to indicate that the advertising loop is active (see `User interface`_ for details).


.. tabs::

   .. group-tab:: nRF Connect for Mobile

      To test the sample using the `nRF Connect for Mobile`_ application, complete the following steps:

      .. tabs::

         .. group-tab:: nRF52 and nRF53 DKs

            1. Install and start the `nRF Connect for Mobile`_ application on your smartphone or tablet.
            #. Power on the development kit or insert your dongle into the USB port.
            #. Connect to the device from the application.
               The device is advertising as ``Nordic_LBS``.
               The services of the connected device are shown.
            #. In **Nordic LED Button Service**, enable notifications for the **Button** characteristic.
            #. Press **Button 1** on the device.
            #. Observe that notifications with the following values are displayed:

               * ``Button released`` when **Button 1** is released.
               * ``Button pressed`` when **Button 1** is pressed.

            #. Write the following values to the LED characteristic in the **Nordic LED Button Service**.
               Depending on the hardware platform, this produces results described in the table.

               +------------------------+---------+----------------------------------------------+
               | Hardware platform      | Value   | Effect                                       |
               +========================+=========+==============================================+
               | nRF52 and nRF53 DKs    | ``OFF`` | Switch the **LED 3** off.                    |
               +                        +---------+----------------------------------------------+
               |                        | ``ON``  | Switch the **LED 3** on.                     |
               +------------------------+---------+----------------------------------------------+
               | nRF52840 Dongle        | ``OFF`` | Switch the green channel of the RGB LED off. |
               +                        +---------+----------------------------------------------+
               |                        | ``ON``  | Switch the green channel of the RGB LED on.  |
               +------------------------+---------+----------------------------------------------+
               | Thingy:53              | ``OFF`` | Switch the blue channel of the RGB LED off.  |
               +                        +---------+----------------------------------------------+
               |                        | ``ON``  | Switch the blue channel of the RGB LED on.   |
               +------------------------+---------+----------------------------------------------+

         .. group-tab:: nRF54 DKs

            .. note::
               |nrf54_buttons_leds_numbering|

            1. Install and start the `nRF Connect for Mobile`_ application on your smartphone or tablet.
            #. Power on the development kit or insert your dongle into the USB port.
            #. Connect to the device from the application.
               The device is advertising as ``Nordic_LBS``.
               The services of the connected device are shown.
            #. In **Nordic LED Button Service**, enable notifications for the **Button** characteristic.
            #. Press **Button 0** on the device.
            #. Observe that notifications with the following values are displayed:

               * ``Button released`` when **Button 0** is released.
               * ``Button pressed`` when **Button 0** is pressed.

            #. Write the following values to the LED characteristic in the **Nordic LED Button Service**.
               Depending on the hardware platform, this produces results described in the table.

               +------------------------+---------+----------------------------------------------+
               | Hardware platform      | Value   | Effect                                       |
               +========================+=========+==============================================+
               | nRF54 DKs              | ``OFF`` | Switch the **LED 2** off.                    |
               +                        +---------+----------------------------------------------+
               |                        | ``ON``  | Switch the **LED 2** on.                     |
               +------------------------+---------+----------------------------------------------+

   .. group-tab:: nRF Blinky

      To test the sample using the nRF Blinky mobile app, see the `nRF Blinky documentation`_.

.. _peripheral_lbs_memfault_relay:

Relaying events to Memfault
============================

To forward the Memfault CBOR events (see `Memfault event relay`_) from the device to your Memfault project, complete the following steps on a PC with a Bluetooth LE adapter:

1. Install `uv <https://docs.astral.sh/uv/getting-started/installation/>`_, if you do not already have it.
   The relay script declares its dependencies (``bleak``, ``cbor2``, ``requests``) as `inline script metadata <https://packaging.python.org/en/latest/specifications/inline-script-metadata/>`_, so ``uv`` installs them automatically into an ephemeral environment - no virtual environment or requirements file needed.
#. Get your Memfault project key, available under :guilabel:`Settings > General` in the Memfault web app.
#. Program the sample to your development kit and let it advertise.
#. Run the relay script, adjusting the arguments to match your project key and device name:

   .. code-block:: console

      uv run samples/bluetooth/peripheral_lbs/scripts/memfault_ble_relay.py \
        --project-key <your-memfault-project-key> \
        --device-name Nordic_LBS

   The script scans for the device, connects, and subscribes to the Memfault Event Data characteristic.
   Each notification is decoded, and its ``device_serial``, ``software_type``, ``software_version``, and ``hardware_version`` fields are used as-is to build a ``heartbeat`` event, which is posted to the Memfault events API with the Bluetooth LE RSSI observed during the connection scan added as the ``rssi_dbm`` metric.
#. Open your project in the Memfault web app and check the device's **Metrics** page to see the ``batt_v``, ``rssi_dbm``, ``button_presses``, and ``uptime_s`` values reported by the sample.

Dependencies
************

This sample uses the following |NCS| libraries:

* :ref:`lbs_readme`
* :ref:`dk_buttons_and_leds_readme`

In addition, it uses the following Zephyr libraries:

* :file:`include/zephyr/types.h`
* :file:`lib/libc/minimal/include/errno.h`
* :file:`include/sys/printk.h`
* :file:`include/sys/byteorder.h`
* :ref:`GPIO Interface <zephyr:api_peripherals>`
* :ref:`zephyr:bluetooth_api`:

  * :file:`include/bluetooth/bluetooth.h`
  * :file:`include/bluetooth/hci.h`
  * :file:`include/bluetooth/conn.h`
  * :file:`include/bluetooth/uuid.h`
  * :file:`include/bluetooth/gatt.h`

It also uses `zcbor <https://github.com/NordicSemiconductor/zcbor>`_ to encode the Memfault events, and the :file:`lib/hw_id` library to derive the simulated Memfault device serial from the Bluetooth LE MAC address (see `Memfault event relay`_).

The sample also uses the following secure firmware component:

* :ref:`Trusted Firmware-M <ug_tfm>`
