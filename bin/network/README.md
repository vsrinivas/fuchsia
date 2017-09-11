# Network

This repository contains the interface and implementation of FIDL
Network Service.

     +-----------+         +-----------+
     | FIDL app  |         | POSIX app |
     +------+----+         +-----+-----+
            |                    |
    +-------v--------------+     |
    | FIDL network service |     |
    |   (//garnet/bin/network)   |     |
    +-------+--------------+     |
            |                    |
      +-----v--------------------v-----+
      |         BSD socket API         |
      |  (//magenta/system/ulib/mxio)  |
      +---------------+----------------+
                      |
        +-------------v--------------+
        |         netstack           |
        | (//garnet/go/src/netstack) |
        +-------------+--------------+
                      |
     +----------------v-----------------+
     |         Ethernet driver          |
     | (//magenta/system/udev/ethernet) |
     +----------------------------------+
