# Netstack

Netstack is a userspace TCP/IP network stack and interfaces with zircon
network drivers.
Netstack serves as a back-end for fdio socket API.

     +-----------+           +-----------+
     | FIDL app  |           | POSIX app |
     +--+--------+           +-----+-----+
        |                          |
        |                          |
        |                          |
        |                          |
        |                          |
        |                          |
        |   +----------------------v---------+
        |   |         BSD socket API         |
        |   |  (//zircon/system/ulib/fdio)   |
        |   +---------+----------------------+
        |             |
     +--v-------------v----------------------+
     |            netstack                   |
     | (//src/connectivity/network/netstack) |
     +----------------+----------------------+
                      |
     +----------------v-----------------+
     |         Ethernet driver          |
     | (//zircon/system/udev/ethernet)  |
     +----------------------------------+
