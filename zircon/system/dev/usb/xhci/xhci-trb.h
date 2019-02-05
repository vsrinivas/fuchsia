// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <zircon/listnode.h>

#include "xhci-hw.h"

namespace usb_xhci {

// used for both command ring and transfer rings
struct xhci_transfer_ring_t {
    ddk::IoBuffer buffer;
    xhci_trb_t* start;
    xhci_trb_t* current;        // next to be filled by producer
    uint8_t pcs;                // producer cycle status
    xhci_trb_t* dequeue_ptr;    // next to be processed by consumer
                                // (not used for command ring)
    size_t size;                // number of TRBs in ring
    bool full;                  // true if there are no available TRBs,
                                // this is needed to differentiate between
                                // an empty and full ring state
};

struct xhci_event_ring_t {
    ddk::IoBuffer buffer;
    xhci_trb_t* start;
    xhci_trb_t* current;
    xhci_trb_t* end;
    uint8_t ccs; // consumer cycle status
};

zx_status_t xhci_transfer_ring_init(xhci_transfer_ring_t* tr, zx_handle_t bti_handle, int count);
void xhci_transfer_ring_free(xhci_transfer_ring_t* ring);
size_t xhci_transfer_ring_free_trbs(xhci_transfer_ring_t* ring);
zx_status_t xhci_event_ring_init(xhci_event_ring_t*, zx_handle_t bti_handle,
                                 erst_entry_t* erst_array, int count);
void xhci_event_ring_free(xhci_event_ring_t* ring);
void xhci_clear_trb(xhci_trb_t* trb);
// Converts a transfer trb into a NO-OP transfer TRB, does nothing if it is the LINK TRB.
void xhci_set_transfer_noop_trb(xhci_trb_t* trb);
xhci_trb_t* xhci_read_trb_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* trb);
xhci_trb_t* xhci_get_next_trb(xhci_transfer_ring_t* ring, xhci_trb_t* trb);
void xhci_increment_ring(xhci_transfer_ring_t* ring);
void xhci_set_dequeue_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* new_ptr);

// Returns the TRB corresponding to the given physical address, or nullptr if the address is invalid.
xhci_trb_t* xhci_transfer_ring_phys_to_trb(xhci_transfer_ring_t* ring, zx_paddr_t phys);

static inline zx_paddr_t xhci_transfer_ring_start_phys(xhci_transfer_ring_t* ring) {
    return ring->buffer.phys();
}

static inline zx_paddr_t xhci_transfer_ring_current_phys(xhci_transfer_ring_t* ring) {
    return ring->buffer.phys() + ((ring->current - ring->start) * sizeof(xhci_trb_t));
}

static inline zx_paddr_t xhci_event_ring_start_phys(xhci_event_ring_t* ring) {
    return ring->buffer.phys();
}

static inline zx_paddr_t xhci_event_ring_current_phys(xhci_event_ring_t* ring) {
    return ring->buffer.phys() + ((ring->current - ring->start) * sizeof(xhci_trb_t));
}

} // namespace usb_xhci
