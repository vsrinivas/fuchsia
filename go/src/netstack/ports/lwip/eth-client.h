// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_PORTS_LWIP_ETH_CLIENT_H
#define APPS_NETSTACK_PORTS_LWIP_ETH_CLIENT_H

#include <magenta/compiler.h>
#include <magenta/device/ethernet.h>
#include <magenta/types.h>

typedef struct eth_client {
  mx_handle_t tx_fifo;
  mx_handle_t rx_fifo;
  uint32_t tx_size;
  uint32_t rx_size;
  void* iobuf;
} eth_client_t;

mx_status_t eth_create(int fd, mx_handle_t io_vmo, void* io_mem,
                       eth_client_t** out);

void eth_destroy(eth_client_t* eth);

// Enqueue a packet for transmit
mx_status_t eth_queue_tx(eth_client_t* eth, void* cookie, void* data,
                         size_t len, uint32_t options);

// Process all transmitted buffers
mx_status_t eth_complete_tx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie));

// Enqueue a packet for reception.
mx_status_t eth_queue_rx(eth_client_t* eth, void* cookie, void* data,
                         size_t len, uint32_t options);

// Process all received buffers
mx_status_t eth_complete_rx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie, size_t len,
                                         uint32_t flags));

// Wait for completed rx packets
// ERR_PEER_CLOSED - far side disconnected
// ERR_TIMED_OUT - deadline lapsed.
// NO_ERROR - completed packets are available
mx_status_t eth_wait_rx(eth_client_t* eth, mx_time_t deadline);

#endif /* APPS_NETSTACK_PORTS_LWIP_ETH_CLIENT_H */
