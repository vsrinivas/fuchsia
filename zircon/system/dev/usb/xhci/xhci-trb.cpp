// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <hw/arch_ops.h>

#include "xhci.h"

namespace usb_xhci {

zx_status_t xhci_transfer_ring_init(xhci_transfer_ring_t* ring, zx_handle_t bti_handle, int count) {
    zx_status_t status = ring->buffer.Init(bti_handle, count * sizeof(xhci_trb_t),
                                           IO_BUFFER_RW | IO_BUFFER_CONTIG |
                                           XHCI_IO_BUFFER_UNCACHED);
    if (status != ZX_OK) return status;

    ring->start = static_cast<xhci_trb_t*>(ring->buffer.virt());
    ring->current = ring->start;
    ring->dequeue_ptr = ring->start;
    ring->full = false;
    ring->size = count - 1;    // subtract 1 for LINK TRB at the end
    ring->pcs = TRB_C;

    // set link TRB at end to point back to the beginning
    ring->start[count - 1].ptr = ring->buffer.phys();
    trb_set_control(&ring->start[count - 1], TRB_LINK, TRB_TC);
    return ZX_OK;
}

void xhci_transfer_ring_free(xhci_transfer_ring_t* ring) {
    ring->buffer.release();
}

// return the number of free TRBs in the ring
size_t xhci_transfer_ring_free_trbs(xhci_transfer_ring_t* ring) {
    xhci_trb_t* current = ring->current;
    xhci_trb_t* dequeue_ptr = ring->dequeue_ptr;

    if (ring->full) {
        assert(current == dequeue_ptr);
        return 0;
    }

    auto size = ring->size;

    if (current < dequeue_ptr) {
        current += size;
    }

    size_t busy_count = current - dequeue_ptr;
    return size - busy_count;
}

zx_status_t xhci_event_ring_init(xhci_event_ring_t* ring, zx_handle_t bti_handle,
                                 erst_entry_t* erst_array, int count) {
    // allocate a read-only buffer for TRBs
    zx_status_t status = ring->buffer.Init(bti_handle, count * sizeof(xhci_trb_t),
                                           IO_BUFFER_RO | IO_BUFFER_CONTIG |
                                           XHCI_IO_BUFFER_UNCACHED);
    if (status != ZX_OK) return status;

    ring->start = static_cast<xhci_trb_t*>(ring->buffer.virt());
    XHCI_WRITE64(&erst_array[0].ptr, ring->buffer.phys());
    XHCI_WRITE32(&erst_array[0].size, count);

    ring->current = ring->start;
    ring->end = ring->start + count;
    ring->ccs = TRB_C;
    return ZX_OK;
}

void xhci_event_ring_free(xhci_event_ring_t* ring) {
    ring->buffer.release();
}

void xhci_clear_trb(xhci_trb_t* trb) {
    XHCI_WRITE64(&trb->ptr, 0);
    XHCI_WRITE32(&trb->status, 0);
    XHCI_WRITE32(&trb->control, 0);
}

void xhci_set_transfer_noop_trb(xhci_trb_t* trb) {
    uint32_t control = XHCI_READ32(&trb->control);
    if ((control & TRB_TYPE_MASK) == (TRB_LINK << TRB_TYPE_START)) {
        // Don't do anything if it's the LINK TRB.
        return;
    }
    XHCI_WRITE64(&trb->ptr, 0);
    XHCI_WRITE32(&trb->status, 0);
    // Preserve the cycle bit of the TRB.
    trb_set_control(trb, TRB_TRANSFER_NOOP, control & TRB_C);
}

xhci_trb_t* xhci_read_trb_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
    // convert physical address to virtual
    uintptr_t ptr = trb->ptr;
    ptr += (reinterpret_cast<uintptr_t>(ring->buffer.virt()) - ring->buffer.phys());
    return reinterpret_cast<xhci_trb_t*>(ptr);
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

    if (ring->current == ring->dequeue_ptr) {
        // We've just enqueued something, so if the pointers are equal,
        // the ring must be full.
        ring->full = true;
    }
}

void xhci_set_dequeue_ptr(xhci_transfer_ring_t* ring, xhci_trb_t* new_ptr) {
    ring->dequeue_ptr = new_ptr;
    ring->full = false;
}

xhci_trb_t* xhci_transfer_ring_phys_to_trb(xhci_transfer_ring_t* ring, zx_paddr_t phys) {
    zx_paddr_t first_trb_phys = xhci_transfer_ring_start_phys(ring);
    // Get the physical address of the start of the last trb,
    // ring->size does not include the LINK TRB at the end of the ring.
    zx_paddr_t last_trb_phys = first_trb_phys + (ring->size * sizeof(xhci_trb_t));

    if (phys < first_trb_phys || phys > last_trb_phys) {
        return nullptr;
    }
   return ring->start + ((phys - first_trb_phys) / sizeof(xhci_trb_t));
}

} // namespace usb_xhci
