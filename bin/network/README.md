# Network

This repository contains the interface and implementation of FIDL
Network Service.

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
