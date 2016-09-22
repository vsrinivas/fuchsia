// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/listnode.h>

#include "xhci-hw.h"

// used for both command ring and transfer rings
typedef struct xhci_transfer_ring {
    xhci_trb_t* start;
    xhci_trb_t* current;        // next to be filled by producer
    uint8_t pcs;                // producer cycle status
    xhci_trb_t* dequeue_ptr;    // next to be processed by consumer
                                // (not used for command ring)
    size_t size;                // number of TRBs in ring

    mtx_t mutex;
    list_node_t pending_requests;   // pending transfers that should be completed when ring is dead
    list_node_t deferred_txns;      // used by upper layer to defer iotxns when ring is full
    bool enabled;
} xhci_transfer_ring_t;

typedef struct xhci_event_ring {
    xhci_trb_t* start;
    xhci_trb_t* current;
    xhci_trb_t* end;
    erst_entry_t* erst_array;
    uint8_t ccs; // consumer cycle status
} xhci_event_ring_t;

typedef struct xhci xhci_t;

mx_status_t xhci_transfer_ring_init(xhci_t* xhci, xhci_transfer_ring_t* tr, int count);
void xhci_transfer_ring_free(xhci_t* xhci, xhci_transfer_ring_t* ring);
size_t xhci_transfer_ring_free_trbs(xhci_transfer_ring_t* ring);
mx_status_t xhci_event_ring_init(xhci_t* xhci, int interruptor, int count);
void xhci_event_ring_free(xhci_t* xhci, int interruptor);
void xhci_clear_trb(xhci_trb_t* trb);
void* xhci_read_trb_ptr(xhci_t* xhci, xhci_trb_t* trb);
xhci_trb_t* xhci_get_next_trb(xhci_t* xhci, xhci_trb_t* trb);
void xhci_increment_ring(xhci_t* xhci, xhci_transfer_ring_t* ring);
