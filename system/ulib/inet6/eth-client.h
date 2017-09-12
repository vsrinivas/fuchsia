// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/device/ethernet.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef struct eth_client {
    zx_handle_t tx_fifo;
    zx_handle_t rx_fifo;
    uint32_t tx_size;
    uint32_t rx_size;
    void* iobuf;
} eth_client_t;

zx_status_t eth_create(int fd, zx_handle_t io_vmo, void* io_mem, eth_client_t** out);

void eth_destroy(eth_client_t* eth);

// Enqueue a packet for transmit
zx_status_t eth_queue_tx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options);

// Process all transmitted buffers
zx_status_t eth_complete_tx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie));

// Enqueue a packet for reception.
zx_status_t eth_queue_rx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options);

// Process all received buffers
zx_status_t eth_complete_rx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie, size_t len, uint32_t flags));

// Wait for completed rx packets
// ZX_ERR_PEER_CLOSED - far side disconnected
// ZX_ERR_TIMED_OUT - deadline lapsed.
// ZX_OK - completed packets are available
zx_status_t eth_wait_rx(eth_client_t* eth, zx_time_t deadline);

__END_CDECLS;
