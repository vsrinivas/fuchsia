// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci.h"

#define MXDEBUG 0
#include <mxio/debug.h>

inline void trb_set_link(xhci_t* xhci, xhci_trb_t* trb, xhci_trb_t* next, bool toggle_cycle) {
    trb_set_ptr(trb, xhci_virt_to_phys(xhci, (mx_vaddr_t)next));
    trb_set_control(trb, TRB_LINK, (toggle_cycle ? TRB_TC : 0));
}

mx_status_t xhci_transfer_ring_init(xhci_t* xhci, xhci_transfer_ring_t* ring, int count) {
    list_initialize(&ring->pending_requests);
    completion_signal(&ring->completion);

    ring->start = xhci_memalign(xhci, 64, count * sizeof(xhci_trb_t));
    if (!ring->start)
        return ERR_NO_MEMORY;
    ring->current = ring->start;
    ring->pcs = TRB_C;
    trb_set_link(xhci, &ring->start[count - 1], ring->start, true);
    ring->dead = false;
    return NO_ERROR;
}

void xhci_transfer_ring_free(xhci_t* xhci, xhci_transfer_ring_t* ring) {
    xhci_free(xhci, (void*)ring->start);
}

mx_status_t xhci_event_ring_init(xhci_t* xhci, int interruptor, int count) {
    xhci_event_ring_t* ring = &xhci->event_rings[interruptor];

    ring->start = xhci_memalign(xhci, 64, count * sizeof(xhci_trb_t));
    if (!ring->start)
        return ERR_NO_MEMORY;
    ring->erst_array = xhci_memalign(xhci, 64, ERST_ARRAY_SIZE * sizeof(erst_entry_t));
    if (!ring->erst_array) {
        xhci_free(xhci, (void*)ring->start);
        return ERR_NO_MEMORY;
    }
    XHCI_WRITE64(&ring->erst_array[0].ptr, xhci_virt_to_phys(xhci, (mx_vaddr_t)ring->start));
    XHCI_WRITE32(&ring->erst_array[0].size, count);

    ring->current = ring->start;
    ring->end = ring->start + count;
    ring->ccs = TRB_C;
    return NO_ERROR;
}

void xhci_clear_trb(xhci_trb_t* trb) {
    XHCI_WRITE64(&trb->ptr, 0);
    XHCI_WRITE32(&trb->status, 0);
    XHCI_WRITE32(&trb->control, 0);
}

void* xhci_read_trb_ptr(xhci_t* xhci, xhci_trb_t* trb) {
    return (void*)xhci_phys_to_virt(xhci, (mx_paddr_t)trb_get_ptr(trb));
}

xhci_trb_t* xhci_get_next_trb(xhci_t* xhci, xhci_trb_t* trb) {
    trb++;
    uint32_t control = XHCI_READ32(&trb->control);
    if ((control & TRB_TYPE_MASK) == (TRB_LINK << TRB_TYPE_START)) {
        trb = xhci_read_trb_ptr(xhci, trb);
    }
    return trb;
}

void xhci_increment_ring(xhci_t* xhci, xhci_transfer_ring_t* ring) {
    xhci_trb_t* trb = ring->current;
    uint32_t control = XHCI_READ32(&trb->control);
    uint32_t chain = control & TRB_CHAIN;
    if (ring->pcs) {
        XHCI_WRITE32(&trb->control, control | ring->pcs);
    }
    trb = ++ring->current;

    // check for LINK TRB
    control = XHCI_READ32(&trb->control);
    if ((control & TRB_TYPE_MASK) == (TRB_LINK << TRB_TYPE_START)) {
        control = (control & ~(TRB_CHAIN | TRB_C)) | chain | ring->pcs;
        XHCI_WRITE32(&trb->control, control);

        // toggle pcs if necessary
        if (control & TRB_TC) {
            ring->pcs ^= TRB_C;
        }
        ring->current = xhci_read_trb_ptr(xhci, trb);
    }
}
