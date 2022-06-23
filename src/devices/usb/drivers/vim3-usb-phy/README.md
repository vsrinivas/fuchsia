Note:
This driver is cloned from aml-usb-phy-v2. There are some differences for Vim3 (A311D Amlogic SoC)
which motivated this cloning.

Key differences are as below -

- Vim3 has 2 USB-A port in addition to the USB-C port. The USB-C port can only act as a peripheral
  device. Therefore, both dwc2 and xhci devices need to be added.
- The USB-A port next to the ethernet supports USB 3.0. Some of the PLL settings and mode settings
  are changed to support USB 3.0.
