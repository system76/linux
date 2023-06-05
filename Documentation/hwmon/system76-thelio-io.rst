.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver system76-thelio-io
==========================

Supported devices:

  * System76 Thelio Io (thelio_io_2)

Author: Jeremy Soller

Description
-----------

This driver implements the sysfs interface for the System76 Thelio Io.
The System76 Thelio Io is a USB device with 4 fan connectors and a
power button LED.

Usage Notes
-----------

Since it is a USB device, hotswapping is possible. The device is autodetected.

Sysfs entries
-------------

======================= =====================================================================
fan[1-4]_input		Connected fan rpm.
fan[1-4]_label		Shows fan connector name.
pwm[1-4]		Sets the fan speed. Values from 0-255.
======================= =====================================================================
