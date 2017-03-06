# Network

This repository contains the interface and implementation of FIDL
Network Service.

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
