# Netstack

Netstack is a userspace TCP/IP network stack and interfaces with zircon
network drivers.
Netstack serves as a back-end for fdio socket API.

     +-----------+           +-----------+
     | FIDL app  |           | POSIX app |
     +------+----+           +-----+-----+
            |                      |
    +-------v----------------+     |
    |  FIDL network service  |     |
    | (//garnet/bin/network) |     |
    +-------+----------------+     |
            |                      |
      +-----v----------------------v---+
      |         BSD socket API         |
      |  (//zircon/system/ulib/fdio)   |
      +---------------+----------------+
                      |
        +-------------v--------------+
        |         netstack           |
        | (//garnet/go/src/netstack) |
        +-------------+--------------+
                      |
     +----------------v-----------------+
     |         Ethernet driver          |
     | (//zircon/system/udev/ethernet)  |
     +----------------------------------+
