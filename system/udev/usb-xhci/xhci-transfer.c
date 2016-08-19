// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hw/usb.h>
#include <stdio.h>
#include <threads.h>

#include "xhci.h"

//#define TRACE 1
#include "xhci-debug.h"

//#define TRACE_TRBS 1
#ifdef TRACE_TRBS
static void print_trb(xhci_t* xhci, xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
    int index = trb - ring->start;
    uint32_t* ptr = (uint32_t *)trb;
    uint64_t paddr = xhci_virt_to_phys(xhci, (mx_vaddr_t)trb);

    printf("trb[%03d] %p: %08X %08X %08X %08X\n", index, (void *)paddr, ptr[0], ptr[1], ptr[2], ptr[3]);
}
#else
#define print_trb(xhci, ring, trb) do {} while (0)
#endif

int xhci_queue_transfer(xhci_t* xhci, int slot_id, usb_setup_t* setup, void* data,
                        uint16_t length, int endpoint, int direction, xhci_transfer_context_t* context) {
    xprintf("xhci_queue_transfer slot_id: %d setup: %p endpoint: %d length: %d\n", slot_id, setup, endpoint, length);

    xhci_slot_t* slot = &xhci->slots[slot_id];
    if (!slot->enabled)
        return ERR_CHANNEL_CLOSED;

    xhci_transfer_ring_t* ring = &slot->transfer_rings[endpoint];
    if (!ring)
        return ERR_INVALID_ARGS;
    if (ring->stalled)
        return ERR_BAD_STATE;

    uint32_t interruptor_target = 0;
    const size_t max_transfer_size = 1 << (XFER_TRB_XFER_LENGTH_BITS - 1);

    // FIXME handle zero length packets

    mtx_lock(&ring->mutex);
    context->transfer_ring = ring;
    list_add_tail(&ring->pending_requests, &context->node);
    completion_reset(&ring->completion);

    if (setup) {
        // Setup Stage
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);

        XHCI_SET_BITS32(&trb->ptr_low, SETUP_TRB_REQ_TYPE_START, SETUP_TRB_REQ_TYPE_BITS, setup->bmRequestType);
        XHCI_SET_BITS32(&trb->ptr_low, SETUP_TRB_REQUEST_START, SETUP_TRB_REQUEST_BITS, setup->bRequest);
        XHCI_SET_BITS32(&trb->ptr_low, SETUP_TRB_VALUE_START, SETUP_TRB_VALUE_BITS, setup->wValue);
        XHCI_SET_BITS32(&trb->ptr_high, SETUP_TRB_INDEX_START, SETUP_TRB_INDEX_BITS, setup->wIndex);
        XHCI_SET_BITS32(&trb->ptr_high, SETUP_TRB_LENGTH_START, SETUP_TRB_LENGTH_BITS, length);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_XFER_LENGTH_START, XFER_TRB_XFER_LENGTH_BITS, 8);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS, interruptor_target);

        uint32_t control_bits = (length == 0 ? XFER_TRB_TRT_NONE : (direction == USB_DIR_IN ? XFER_TRB_TRT_IN : XFER_TRB_TRT_OUT));
        control_bits |= XFER_TRB_IDT; // immediate data flag
        trb_set_control(trb, TRB_TRANSFER_SETUP, control_bits);
        print_trb(xhci, ring, trb);
        xhci_increment_ring(xhci, ring);
    }

    // Data Stage
    if (length > 0) {
        size_t packet_count = (length + max_transfer_size - 1) / max_transfer_size;
        size_t remaining = length;

        for (size_t i = 0; i < packet_count; i++) {
            size_t transfer_size = (remaining > max_transfer_size ? max_transfer_size : remaining);
            remaining -= transfer_size;

            xhci_trb_t* trb = ring->current;
            xhci_clear_trb(trb);
            XHCI_WRITE64(&trb->ptr, xhci_virt_to_phys(xhci, (mx_vaddr_t)data + (i * max_transfer_size)));
            XHCI_SET_BITS32(&trb->status, XFER_TRB_XFER_LENGTH_START, XFER_TRB_XFER_LENGTH_BITS, transfer_size);
            uint32_t td_size = packet_count - i - 1;
            XHCI_SET_BITS32(&trb->status, XFER_TRB_TD_SIZE_START, XFER_TRB_TD_SIZE_BITS, td_size);
            XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS, interruptor_target);

            uint32_t control_bits = TRB_CHAIN;
            if (td_size == 0) {
                control_bits |= XFER_TRB_ENT;
            }
            if (setup && i == 0) {
                // use TRB_TRANSFER_DATA for first data packet on setup requests
                control_bits |= (direction == USB_DIR_IN ? XFER_TRB_DIR_IN : XFER_TRB_DIR_OUT);
                trb_set_control(trb, TRB_TRANSFER_DATA, control_bits);
            } else {
                trb_set_control(trb, TRB_TRANSFER_NORMAL, control_bits);
            }
            print_trb(xhci, ring, trb);
            xhci_increment_ring(xhci, ring);
        }

        // Follow up with event data TRB
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        XHCI_WRITE64(&trb->ptr, (uint64_t)context);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS, interruptor_target);
        trb_set_control(trb, TRB_TRANSFER_EVENT_DATA, XFER_TRB_IOC);
        print_trb(xhci, ring, trb);
        xhci_increment_ring(xhci, ring);
    }

    if (setup) {
        // Status Stage
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS, interruptor_target);
        uint32_t control_bits = (direction == USB_DIR_IN && length > 0 ? XFER_TRB_DIR_OUT : XFER_TRB_DIR_IN);
        if (length == 0) {
            control_bits |= TRB_CHAIN;
        }
        trb_set_control(trb, TRB_TRANSFER_STATUS, control_bits);
        print_trb(xhci, ring, trb);
        xhci_increment_ring(xhci, ring);

        if (length == 0) {
            // Follow up with event data TRB
            xhci_trb_t* trb = ring->current;
            xhci_clear_trb(trb);
            XHCI_WRITE64(&trb->ptr, (uint64_t)context);
            XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS, interruptor_target);
            trb_set_control(trb, TRB_TRANSFER_EVENT_DATA, XFER_TRB_IOC);
            print_trb(xhci, ring, trb);
            xhci_increment_ring(xhci, ring);
        }
    }

    XHCI_WRITE32(&xhci->doorbells[slot_id], endpoint + 1);

    mtx_unlock(&ring->mutex);

    return NO_ERROR;
}

int xhci_control_request(xhci_t* xhci, int slot_id, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void* data, uint16_t length) {
    xprintf("xhci_control_request slot_id: %d type: 0x%02X req: %d value: %d index: %d length: %d\n",
            slot_id, request_type, request, value, index, length);

    usb_setup_t setup;
    setup.bmRequestType = request_type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = length;

    xhci_sync_transfer_t xfer;
    xhci_sync_transfer_init(&xfer);

    mx_status_t result = xhci_queue_transfer(xhci, slot_id, &setup, data, length, 0,
                                             request_type & USB_DIR_MASK, &xfer.transfer_context);
    if (result != NO_ERROR)
        return result;

    result = xhci_sync_transfer_wait(&xfer);
    xprintf("xhci_control_request returning %d\n", result);
    return result;
}

mx_status_t xhci_get_descriptor(xhci_t* xhci, int slot_id, uint8_t type, uint16_t value,
                                uint16_t index, void* data, uint16_t length) {
    return xhci_control_request(xhci, slot_id, USB_DIR_IN | type | USB_RECIP_DEVICE,
                                USB_REQ_GET_DESCRIPTOR, value, index, data, length);
}

void xhci_handle_transfer_event(xhci_t* xhci, xhci_trb_t* trb) {
    xprintf("xhci_handle_transfer_event: %08X %08X %08X %08X\n",
            ((uint32_t*)trb)[0], ((uint32_t*)trb)[1], ((uint32_t*)trb)[2], ((uint32_t*)trb)[3]);

    uint32_t control = XHCI_READ32(&trb->control);
    uint32_t status = XHCI_READ32(&trb->status);
    uint32_t cc = (status & XHCI_MASK(EVT_TRB_CC_START, EVT_TRB_CC_BITS)) >> EVT_TRB_CC_START;
    uint32_t length = (status & XHCI_MASK(EVT_TRB_XFER_LENGTH_START, EVT_TRB_XFER_LENGTH_BITS)) >> EVT_TRB_XFER_LENGTH_START;
    xhci_transfer_context_t* context = NULL;

    if (control & EVT_TRB_ED) {
        context = (xhci_transfer_context_t*)trb->ptr;
    } else {
        trb = xhci_read_trb_ptr(xhci, trb);
        for (int i = 0; i < 5 && trb; i++) {
            if (trb_get_type(trb) == TRB_TRANSFER_EVENT_DATA) {
                context = (xhci_transfer_context_t*)trb->ptr;
                break;
            }
            trb = xhci_get_next_trb(xhci, trb);
        }
    }

    mx_status_t result;
    switch (cc) {
        case TRB_CC_SUCCESS:
        case TRB_CC_SHORT_PACKET:
            result = length;
            break;
        case TRB_CC_STALL_ERROR:
            // FIXME - better error for stall case?
            result = ERR_BAD_STATE;
            break;
        case TRB_CC_STOPPED:
        case TRB_CC_STOPPED_LENGTH_INVALID:
        case TRB_CC_STOPPED_SHORT_PACKET:
            // for these errors the transfer ring may no longer exist,
            // so it is not safe to attempt to retrieve our transfer context
            xprintf("ignoring transfer event with cc: %d\n", cc);
            return;
        default:
            // FIXME - how do we report stalls, etc?
            result = ERR_CHANNEL_CLOSED;
            break;
    }

    if (!context) {
        printf("unable to find transfer context in xhci_handle_transfer_event\n");
        return;
    }

    xhci_transfer_ring_t* ring = context->transfer_ring;

    mtx_lock(&ring->mutex);

    if (cc == TRB_CC_STALL_ERROR) {
        ring->stalled = true;
    }

    list_delete(&context->node);
    if (list_is_empty(&ring->pending_requests)) {
        completion_signal(&ring->completion);
    }

    mtx_unlock(&ring->mutex);

    context->callback(result, context->data);

    if (ring->dead) {
        // once we get a transfer error on a dead endpoint we will receive no more events.
        // so complete all remaining pending requests
        // FIXME - find a better way to handle this
        list_for_every_entry (&ring->pending_requests, context, xhci_transfer_context_t, node) {
            context->callback(ERR_CHANNEL_CLOSED, context->data);
        }
        list_initialize(&ring->pending_requests);
        completion_signal(&ring->completion);
    }
}

static void xhci_sync_transfer_callback(mx_status_t result, void* data) {
    xhci_sync_transfer_t* xfer = (xhci_sync_transfer_t*)data;
    xfer->result = result;
    completion_signal(&xfer->completion);
}

void xhci_transfer_context_init(xhci_transfer_context_t* xfer,
                                void (*callback)(mx_status_t result, void* data), void* data) {
    xfer->callback = callback;
    xfer->data = data;
}

void xhci_sync_transfer_init(xhci_sync_transfer_t* xfer) {
    completion_reset(&xfer->completion);
    xhci_transfer_context_init(&xfer->transfer_context, xhci_sync_transfer_callback, xfer);
}

mx_status_t xhci_sync_transfer_wait(xhci_sync_transfer_t* xfer) {
    completion_wait(&xfer->completion, MX_TIME_INFINITE);
    return xfer->result;
}
