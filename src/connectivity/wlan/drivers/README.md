## Drivers

This repository contains drivers for wireless devices.

A Full MAC driver relies on the firmware in the wireless hardware to implement
the majority of the IEEE 802.11 MLME functions.

A Soft MAC driver implements the basic building blocks of communication with the
wireless hardware in order to allow the Fuchsia MLME driver to execute the IEEE
802.11 MLME functions.

The Fuchsia MLME driver is a hardware-independent layer that provides state
machines for synchronization, authentication, association, and other wireless
networking state. It communicates with a Soft MAC driver to manage the hardware.
