// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "xhci-transfer-common.h"

void xhci_print_trb(xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
    int index = trb - ring->start;
    uint32_t* ptr = (uint32_t *)trb;
    uint64_t paddr = io_buffer_phys(&ring->buffer) + index * sizeof(xhci_trb_t);

    zxlogf(LSPEW, "trb[%03d] %p: %08X %08X %08X %08X\n", index, (void *)paddr, ptr[0], ptr[1], ptr[2], ptr[3]);
}

zx_status_t xhci_queue_data_trbs(xhci_transfer_ring_t* ring, xhci_transfer_state_t* state,
                                 usb_request_t* req, int interrupter_target, bool isochronous) {
    usb_header_t* header = &req->header;
    uint64_t frame = header->frame;
    size_t free_trbs = xhci_transfer_ring_free_trbs(ring);

    zx_paddr_t paddr;
    size_t transfer_size = 0;
    bool first_packet = (state->phys_iter.offset == 0);
    while (free_trbs > 0 && (((transfer_size = usb_request_phys_iter_next(&state->phys_iter, &paddr)) > 0) ||
                             state->needs_transfer_trb || state->needs_zlp)) {
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        XHCI_WRITE64(&trb->ptr, paddr);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_XFER_LENGTH_START, XFER_TRB_XFER_LENGTH_BITS,
                        transfer_size);
        // number of packets remaining after this one
        uint32_t td_size = --state->packet_count;
        if (state->needs_zlp) {
            td_size++;
        }
        XHCI_SET_BITS32(&trb->status, XFER_TRB_TD_SIZE_START, XFER_TRB_TD_SIZE_BITS, td_size);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS,
                        interrupter_target);
        uint32_t control_bits = TRB_CHAIN;
        if (td_size == 0) {
            control_bits |= XFER_TRB_ENT;
        }
        if (header->ep_address == 0 && first_packet) {
            // use TRB_TRANSFER_DATA for first data packet on setup requests
            control_bits |= (state->direction == USB_DIR_IN ? XFER_TRB_DIR_IN : XFER_TRB_DIR_OUT);
            trb_set_control(trb, TRB_TRANSFER_DATA, control_bits);
        } else if (isochronous && first_packet) {
            // use TRB_TRANSFER_ISOCH for first data packet on isochronous endpoints
            if (frame == 0) {
                // set SIA bit to schedule packet ASAP
                control_bits |= XFER_TRB_SIA;
            } else {
                // schedule packet for specified frame
                control_bits |= (((frame % 2048) << XFER_TRB_FRAME_ID_START) &
                                 XHCI_MASK(XFER_TRB_FRAME_ID_START, XFER_TRB_FRAME_ID_BITS));
           }
            trb_set_control(trb, TRB_TRANSFER_ISOCH, control_bits);
        } else {
            trb_set_control(trb, TRB_TRANSFER_NORMAL, control_bits);
        }
        if (driver_get_log_flags() & DDK_LOG_SPEW) xhci_print_trb(ring, trb);
        xhci_increment_ring(ring);
        free_trbs--;

        first_packet = false;
        state->needs_transfer_trb = false;
        if (transfer_size == 0) {
            // ZLP (if there was one) has been sent
            state->needs_zlp = false;
        }
    }

    if (state->phys_iter.offset < header->length) {
        // still more data to queue, but we are out of TRBs.
        // come back and finish later.
        return ZX_ERR_SHOULD_WAIT;
    }

    if (state->needs_data_event) {
        if (free_trbs == 0) {
            // will need to do this later
            return ZX_ERR_SHOULD_WAIT;
        }

        // Queue event data TRB
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        trb_set_ptr(trb, req);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS,
                        interrupter_target);
        trb_set_control(trb, TRB_TRANSFER_EVENT_DATA, XFER_TRB_IOC);
        if (driver_get_log_flags() & DDK_LOG_SPEW) xhci_print_trb(ring, trb);
        xhci_increment_ring(ring);
        free_trbs--;
        state->needs_data_event = false;
    }
    return ZX_OK;
}