# Network

This repository contains the FIDL HTTP Service protocol and implementation.

     +-----------+           +-----------+
     | FIDL app  |           | POSIX app |
     +------+----+           +-----+-----+
            |                      |
     +-------v----------------+    |
     |  FIDL http service     |    |
     | (//garnet/bin/http)    |    |
     +-------+----------------+    |
            |                      |
     +------v----------------------v--+
     |         BSD socket API         |
     |  (//zircon/system/ulib/fdio)   |
     +----------------+---------------+
                      |
     +----------------v----------------------+
     |            netstack                   |
     | (//src/connectivity/network/netstack) |
     +----------------+----------------------+
                      |
     +----------------v-----------------+
     |         Ethernet driver          |
     | (//zircon/system/udev/ethernet)  |
     +----------------------------------+
