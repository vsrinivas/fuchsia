# Netstack

Netstack is a userspace TCP/IP network stack and interfaces with magenta
network drivers.
Netstack serves as a back-end for mxio socket API.

     +-----------+         +-----------+
     | FIDL app  |         | POSIX app |
     +------+----+         +-----+-----+
            |                    |
    +-------v--------------+     |
    | FIDL network service |     |
    |   (//apps/network)   |     |
    +-------+--------------+     |
            |                    |
      +-----v--------------------v-----+
      |         BSD socket API         |
      |  (//magenta/system/ulib/mxio)  |
      +---------------+----------------+
                      |
        +-------------v--------------+
        |         netstack           |
        |     (//apps/netstack)      |
        +-------------+--------------+
                      |
     +----------------v-----------------+
     |         Ethernet driver          |
     | (//magenta/system/udev/ethernet) |
     +----------------------------------+
