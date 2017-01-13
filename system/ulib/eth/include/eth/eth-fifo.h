// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/ethernet.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

// Fifo creation
mx_status_t eth_fifo_create(uint32_t rx_entries, uint32_t tx_entries, uint32_t options,
        eth_fifo_t* out);
mx_status_t eth_fifo_clone_consumer(eth_fifo_t* in, eth_fifo_t* out);
mx_status_t eth_fifo_clone_producer(eth_fifo_t* in, eth_fifo_t* out);
void eth_fifo_cleanup(eth_fifo_t* fifo);

// Fifo entry mapping
mx_status_t eth_fifo_map_rx_entries(eth_fifo_t* fifo, void* addr);
mx_status_t eth_fifo_map_tx_entries(eth_fifo_t* fifo, void* addr);

__END_CDECLS;
