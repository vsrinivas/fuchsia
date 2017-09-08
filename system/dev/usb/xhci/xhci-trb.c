// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci.h"

mx_status_t xhci_transfer_ring_init(xhci_transfer_ring_t* ring, int count) {
    mx_status_t status = io_buffer_init(&ring->buffer, count * sizeof(xhci_trb_t), IO_BUFFER_RW);
    if (status != MX_OK) return status;

    ring->start = io_buffer_virt(&ring->buffer);
    ring->current = ring->start;
    ring->dequeue_ptr = ring->start;
    ring->size = count - 1;    // subtract 1 for LINK TRB at the end
    ring->pcs = TRB_C;

    // set link TRB at end to point back to the beginning
    trb_set_ptr(&ring->start[count - 1], (void *)io_buffer_phys(&ring->buffer));
    trb_set_control(&ring->start[count - 1], TRB_LINK, TRB_TC);
    return MX_OK;
}

void xhci_transfer_ring_free(xhci_transfer_ring_t* ring) {
    io_buffer_release(&ring->buffer);
}

// return the number of free TRBs in the ring
size_t xhci_transfer_ring_free_trbs(xhci_transfer_ring_t* ring) {
    xhci_trb_t* current = ring->current;
    xhci_trb_t* dequeue_ptr = ring->dequeue_ptr;
    int size = ring->size;

    if (current < dequeue_ptr) {
        current += size;
    }

    int busy_count = current - dequeue_ptr;
    return size - busy_count;
}

mx_status_t xhci_event_ring_init(xhci_t* xhci, int interrupter, int count) {
    xhci_event_ring_t* ring = &xhci->event_rings[interrupter];
    // allocate buffer for TRBs
    mx_status_t status = io_buffer_init(&ring->buffer, count * sizeof(xhci_trb_t), IO_BUFFER_RW);
    if (status != MX_OK) return status;

    ring->start = io_buffer_virt(&ring->buffer);
    erst_entry_t* erst_array = xhci->erst_arrays[interrupter];
    XHCI_WRITE64(&erst_array[0].ptr, io_buffer_phys(&ring->buffer));
    XHCI_WRITE32(&erst_array[0].size, count);

    ring->current = ring->start;
    ring->end = ring->start + count;
    ring->ccs = TRB_C;
    return MX_OK;
}

void xhci_event_ring_free(xhci_t* xhci, int interrupter) {
    xhci_event_ring_t* ring = &xhci->event_rings[interrupter];
    io_buffer_release(&ring->buffer);
}

void xhci_clear_trb(xhci_trb_t* trb) {
    XHCI_WRITE64(&trb->ptr, 0);
    XHCI_WRITE32(&trb->status, 0);
    XHCI_WRITE32(&trb->control, 0);
}

void* xhci_read_trb_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
    // convert physical address to virtual
    uint8_t* ptr = trb_get_ptr(trb);
    ptr += ((uint8_t *)io_buffer_virt(&ring->buffer) - (uint8_t *)io_buffer_phys(&ring->buffer));
    return ptr;
}

xhci_trb_t* xhci_get_next_trb(xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
    trb++;
    uint32_t control = XHCI_READ32(&trb->control);
    if ((control & TRB_TYPE_MASK) == (TRB_LINK << TRB_TYPE_START)) {
        trb = xhci_read_trb_ptr(ring, trb);
    }
    return trb;
}

void xhci_increment_ring(xhci_transfer_ring_t* ring) {
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
        ring->current = xhci_read_trb_ptr(ring, trb);
    }
}
