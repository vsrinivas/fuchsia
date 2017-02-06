// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/ethernet.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

typedef struct eth_client eth_client_t;

typedef struct eth_client_args {
    // number of rx and tx queue entries
    // must be powers of two
    uint32_t rx_entries;
    uint32_t tx_entries;

    // vmo and local address of an io buffer
    // all packet data sent and received must
    // be within this buffer
    mx_handle_t iobuf_vmo;
    void* iobuf;
} eth_client_args_t;

mx_status_t eth_create(int fd, eth_client_args_t* args, eth_client_t** out);

void eth_destroy(eth_client_t* eth);



// Enqueue a packet for transmit
mx_status_t eth_queue_tx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options);

// Process all transmitted buffers
mx_status_t eth_complete_tx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie));

// Enqueue a packet for reception.
mx_status_t eth_queue_rx(eth_client_t* eth, void* cookie,
                         void* data, size_t len, uint32_t options);

// Process all received buffers
mx_status_t eth_complete_rx(eth_client_t* eth, void* ctx,
                            void (*func)(void* ctx, void* cookie, size_t len, uint32_t flags));

// Wait for completed rx packets
// ERR_REMOTE_CLOSED - far side disconnected
// ERR_TIMED_OUT - timeout expired
// NO_ERROR - completed packets are available
mx_status_t eth_wait_rx(eth_client_t* eth, mx_time_t timeout);




__END_CDECLS;
