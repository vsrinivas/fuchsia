// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_ETHERNET_H_
#define SYSROOT_ZIRCON_DEVICE_ETHERNET_H_

#include <stdint.h>

// flags values for response messages
#define ETH_FIFO_RX_OK (1u)    // packet received okay
#define ETH_FIFO_TX_OK (1u)    // packet transmitted okay
#define ETH_FIFO_INVALID (2u)  // packet not within io_vmo bounds
#define ETH_FIFO_RX_TX (4u)    // received our own tx packet (when TX_LISTEN)

typedef struct eth_fifo_entry {
  // offset from start of io vmo to packet data
  uint32_t offset;
  // length of packet data to tx or rx
  uint16_t length;
  uint16_t flags;
  // cookie
  uint64_t cookie;
} eth_fifo_entry_t;

#endif  // SYSROOT_ZIRCON_DEVICE_ETHERNET_H_
