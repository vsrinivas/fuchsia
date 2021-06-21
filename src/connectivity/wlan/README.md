# Fuchsia WLAN

The WLAN stack is split into several layers:
    - [Policy](/src/connectivity/wlan/wlancfg/README.md) implements interfaces that are used by applications running on Fuchsia.
    - [Core](/src/connectivity/wlan/wlanstack/README.md) implements the 802.11 SME interfaces.
    - [Drivers](/src/connectivity/wlan/drivers/README.md) contains driver implementations for various hardware.
