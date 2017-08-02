// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <magenta/listnode.h>

#include "xhci-hw.h"

// used for both command ring and transfer rings
typedef struct xhci_transfer_ring {
    io_buffer_t buffer;
    xhci_trb_t* start;
    xhci_trb_t* current;        // next to be filled by producer
    uint8_t pcs;                // producer cycle status
    xhci_trb_t* dequeue_ptr;    // next to be processed by consumer
                                // (not used for command ring)
    size_t size;                // number of TRBs in ring
} xhci_transfer_ring_t;

typedef struct xhci_event_ring {
    io_buffer_t buffer;
    xhci_trb_t* start;
    xhci_trb_t* current;
    xhci_trb_t* end;
    uint8_t ccs; // consumer cycle status
} xhci_event_ring_t;

typedef struct xhci xhci_t;

mx_status_t xhci_transfer_ring_init(xhci_transfer_ring_t* tr, int count);
void xhci_transfer_ring_free(xhci_transfer_ring_t* ring);
size_t xhci_transfer_ring_free_trbs(xhci_transfer_ring_t* ring);
mx_status_t xhci_event_ring_init(xhci_t* xhci, int interrupter, int count);
void xhci_event_ring_free(xhci_t* xhci, int interrupter);
void xhci_clear_trb(xhci_trb_t* trb);
void* xhci_read_trb_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* trb);
xhci_trb_t* xhci_get_next_trb(xhci_transfer_ring_t* ring, xhci_trb_t* trb);
void xhci_increment_ring(xhci_transfer_ring_t* ring);

static inline mx_paddr_t xhci_transfer_ring_start_phys(xhci_transfer_ring_t* ring) {
    return io_buffer_phys(&ring->buffer);
}

static inline mx_paddr_t xhci_transfer_ring_current_phys(xhci_transfer_ring_t* ring) {
    return io_buffer_phys(&ring->buffer) + ((ring->current - ring->start) * sizeof(xhci_trb_t));
}

static inline mx_paddr_t xhci_event_ring_start_phys(xhci_event_ring_t* ring) {
    return io_buffer_phys(&ring->buffer);
}

static inline mx_paddr_t xhci_event_ring_current_phys(xhci_event_ring_t* ring) {
    return io_buffer_phys(&ring->buffer) + ((ring->current - ring->start) * sizeof(xhci_trb_t));
}
