# Network

This repository contains the interface and implementation of FIDL
HTTP Service.

     +-----------+           +-----------+
     | FIDL app  |           | POSIX app |
     +------+----+           +-----+-----+
            |                      |
    +-------v----------------+     |
    |  FIDL http service     |     |
    | (//garnet/bin/http)    |     |
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
