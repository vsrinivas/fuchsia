// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/hw/usb.h>
#include <stdio.h>
#include <threads.h>
#include <ddk/protocol/usb.h>

#include "xhci-transfer.h"
#include "xhci-util.h"

//#define TRACE 1
#include "xhci-debug.h"

//#define TRACE_TRBS 1
#ifdef TRACE_TRBS
static void print_trb(xhci_t* xhci, xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
    int index = trb - ring->start;
    uint32_t* ptr = (uint32_t *)trb;
    uint64_t paddr = io_buffer_phys(&ring->buffer, index * sizeof(xhci_trb_t));

    printf("trb[%03d] %p: %08X %08X %08X %08X\n", index, (void *)paddr, ptr[0], ptr[1], ptr[2], ptr[3]);
}
#else
#define print_trb(xhci, ring, trb) do {} while (0)
#endif

// reads a range of bits from an integer
#define READ_FIELD(i, start, bits) (((i) >> (start)) & ((1 << (bits)) - 1))

mx_status_t xhci_reset_endpoint(xhci_t* xhci, uint32_t slot_id, uint32_t endpoint) {
    xprintf("xhci_reset_endpoint %d %d\n", slot_id, endpoint);

    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[endpoint];
    xhci_transfer_ring_t* transfer_ring = &ep->transfer_ring;

    mtx_lock(&ep->lock);

    // first reset the endpoint
    xhci_sync_command_t command;
    xhci_sync_command_init(&command);
    // command expects device context index, so increment endpoint by 1
    uint32_t control = (slot_id << TRB_SLOT_ID_START) | ((endpoint + 1) << TRB_ENDPOINT_ID_START);
    xhci_post_command(xhci, TRB_CMD_RESET_ENDPOINT, 0, control, &command.context);
    int cc = xhci_sync_command_wait(&command);

    if (cc != TRB_CC_SUCCESS) {
        mtx_unlock(&ep->lock);
        return ERR_INTERNAL;
    }

    // then move transfer ring's dequeue pointer passed the failed transaction
    xhci_sync_command_init(&command);
    uint64_t ptr = xhci_transfer_ring_current_phys(transfer_ring);
    ptr |= transfer_ring->pcs;
    // command expects device context index, so increment endpoint by 1
    control = (slot_id << TRB_SLOT_ID_START) | ((endpoint + 1) << TRB_ENDPOINT_ID_START);
    xhci_post_command(xhci, TRB_CMD_SET_TR_DEQUEUE, ptr, control, &command.context);
    cc = xhci_sync_command_wait(&command);

    transfer_ring->dequeue_ptr = transfer_ring->current;

    mtx_unlock(&ep->lock);

    return (cc == TRB_CC_SUCCESS ? NO_ERROR : ERR_INTERNAL);
}

mx_status_t xhci_queue_transfer(xhci_t* xhci, iotxn_t* txn) {
    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);
    int rh_index = xhci_get_root_hub_index(xhci, proto_data->device_id);
    if (rh_index >= 0) {
        return xhci_rh_iotxn_queue(xhci, txn, rh_index);
    }
    uint32_t slot_id = proto_data->device_id;
    if (slot_id > xhci->max_slots) {
        return ERR_INVALID_ARGS;
    }
    uint8_t endpoint = xhci_endpoint_index(proto_data->ep_address);
    if (endpoint >= XHCI_NUM_EPS) {
        return ERR_INVALID_ARGS;
    }

    xprintf("xhci_queue_transfer slot_id: %d setup: %p endpoint: %d length: %d\n",
            slot_id, setup, endpoint, length);

    usb_setup_t* setup = (endpoint == 0 ? &proto_data->setup : NULL);
    if ((setup && endpoint != 0) || (!setup && endpoint == 0)) {
        return ERR_INVALID_ARGS;
    }
    if (slot_id < 1 || slot_id >= xhci->max_slots) {
        return ERR_INVALID_ARGS;
    }

    size_t length = txn->length;
    iotxn_sg_t* sg;
    uint32_t sgl;
    if (length > 0) {
        iotxn_physmap(txn, &sg, &sgl);
    }
    uint64_t frame = proto_data->frame;
    uint8_t direction;
    if (setup) {
        direction = setup->bmRequestType & USB_ENDPOINT_DIR_MASK;
    } else {
        direction = proto_data->ep_address & USB_ENDPOINT_DIR_MASK;
    }

    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[endpoint];
    xhci_transfer_ring_t* ring = &ep->transfer_ring;
    if (!ep->enabled)
        return ERR_REMOTE_CLOSED;

    xhci_endpoint_context_t* epc = slot->eps[endpoint].epc;
    if (XHCI_GET_BITS32(&epc->epc0, EP_CTX_EP_STATE_START, EP_CTX_EP_STATE_BITS) == 2 /* halted */ ) {
        return ERR_IO_REFUSED;
    }

    uint32_t interruptor_target = 0;
    size_t data_packets = (length + XHCI_MAX_DATA_BUFFER - 1) / XHCI_MAX_DATA_BUFFER;
    size_t required_trbs = data_packets + 1;   // add 1 for event data TRB
    if (setup) {
        required_trbs += 2;
    }
    if (required_trbs > ring->size) {
        // no way this will ever succeed
        printf("required_trbs %zu ring->size %zu\n", required_trbs, ring->size);
        return ERR_INVALID_ARGS;
    }

    uint32_t ep_type = XHCI_GET_BITS32(&epc->epc1, EP_CTX_EP_TYPE_START, EP_CTX_EP_TYPE_BITS);
    if (ep_type >= 4) ep_type -= 4;
    bool isochronous = (ep_type == USB_ENDPOINT_ISOCHRONOUS);
    if (isochronous) {
        if (!sg->paddr || !length) return ERR_INVALID_ARGS;
        // we currently do not support isoch buffers that span page boundaries
        // Section 3.2.11 in the XHCI spec describes how to handle this, but since
        // iotxn buffers are always close to the beginning of a page, this shouldn't be necessary.
        mx_paddr_t start_page = sg->paddr & ~(xhci->page_size - 1);
        mx_paddr_t end_page = (sg->paddr + length - 1) & ~(xhci->page_size - 1);
        if (start_page != end_page) {
            printf("isoch buffer spans page boundary in xhci_queue_transfer\n");
            return ERR_INVALID_ARGS;
        }
    }
    if (frame != 0) {
        if (!isochronous) {
            printf("frame scheduling only supported for isochronous transfers\n");
            return ERR_INVALID_ARGS;
        }
        uint64_t current_frame = xhci_get_current_frame(xhci);
        if (frame < current_frame) {
            printf("can't schedule transfer into the past\n");
            return ERR_INVALID_ARGS;
        }
        if (frame - current_frame >= 895) {
            // See XHCI spec, section 4.11.2.5
            printf("can't schedule transfer more than 895ms into the future\n");
            return ERR_INVALID_ARGS;
        }
    }

    // FIXME handle zero length packets

    mtx_lock(&ep->lock);

    // don't allow queueing new requests if we have deferred requests
    if (!list_is_empty(&ep->deferred_txns) || required_trbs > xhci_transfer_ring_free_trbs(ring)) {
        list_add_tail(&ep->deferred_txns, &txn->node);
        mtx_unlock(&ep->lock);
        return ERR_BUFFER_TOO_SMALL;
    }

    list_add_tail(&ep->pending_requests, &txn->node);

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
        xhci_increment_ring(ring);
    }

    // Data Stage
    if (length > 0) {
        size_t remaining = length;

        for (size_t i = 0; i < data_packets; i++) {
            size_t transfer_size = (remaining > XHCI_MAX_DATA_BUFFER ? XHCI_MAX_DATA_BUFFER : remaining);
            remaining -= transfer_size;

            xhci_trb_t* trb = ring->current;
            xhci_clear_trb(trb);
            XHCI_WRITE64(&trb->ptr, sg->paddr + (i * XHCI_MAX_DATA_BUFFER));
            XHCI_SET_BITS32(&trb->status, XFER_TRB_XFER_LENGTH_START, XFER_TRB_XFER_LENGTH_BITS, transfer_size);
            uint32_t td_size = data_packets - i - 1;
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
            } else if (isochronous) {
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
            print_trb(xhci, ring, trb);
            xhci_increment_ring(ring);
        }

        // Follow up with event data TRB
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        trb_set_ptr(trb, txn);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS, interruptor_target);
        trb_set_control(trb, TRB_TRANSFER_EVENT_DATA, XFER_TRB_IOC);
        print_trb(xhci, ring, trb);
        xhci_increment_ring(ring);
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
        xhci_increment_ring(ring);

        if (length == 0) {
            // Follow up with event data TRB
            xhci_trb_t* trb = ring->current;
            xhci_clear_trb(trb);
            trb_set_ptr(trb, txn);
            XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS, interruptor_target);
            trb_set_control(trb, TRB_TRANSFER_EVENT_DATA, XFER_TRB_IOC);
            print_trb(xhci, ring, trb);
            xhci_increment_ring(ring);
        }
    }
    // update dequeue_ptr to TRB following this transaction
    txn->context = (void *)ring->current;

    XHCI_WRITE32(&xhci->doorbells[slot_id], endpoint + 1);

    mtx_unlock(&ep->lock);

    return NO_ERROR;
}

static void xhci_control_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

int xhci_control_request(xhci_t* xhci, uint32_t slot_id, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void* data, uint16_t length) {

    xprintf("xhci_control_request slot_id: %d type: 0x%02X req: %d value: %d index: %d length: %d\n",
            slot_id, request_type, request, value, index, length);

    iotxn_t* txn;

    mx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS | IOTXN_ALLOC_POOL, length);
    if (status != NO_ERROR) return status;
    txn->protocol = MX_PROTOCOL_USB;

    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);

    usb_setup_t* setup = &proto_data->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
    proto_data->device_id = slot_id;
    proto_data->ep_address = 0;
    proto_data->frame = 0;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        iotxn_copyto(txn, data, length, 0);
    }

    completion_t completion = COMPLETION_INIT;

    txn->length = length;
    txn->complete_cb = xhci_control_complete;
    txn->cookie = &completion;
    iotxn_queue(&xhci->device, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    status = txn->status;
    if (status == NO_ERROR) {
        status = txn->actual;

        if (length > 0 && !out) {
            iotxn_copyfrom(txn, data, txn->actual, 0);
        }
    }
    iotxn_release(txn);
    xprintf("xhci_control_request returning %d\n", status);
    return status;
}

mx_status_t xhci_get_descriptor(xhci_t* xhci, uint32_t slot_id, uint8_t type, uint16_t value,
                                uint16_t index, void* data, uint16_t length) {
    return xhci_control_request(xhci, slot_id, USB_DIR_IN | type | USB_RECIP_DEVICE,
                                USB_REQ_GET_DESCRIPTOR, value, index, data, length);
}

void xhci_handle_transfer_event(xhci_t* xhci, xhci_trb_t* trb) {
    xprintf("xhci_handle_transfer_event: %08X %08X %08X %08X\n",
            ((uint32_t*)trb)[0], ((uint32_t*)trb)[1], ((uint32_t*)trb)[2], ((uint32_t*)trb)[3]);

    uint32_t control = XHCI_READ32(&trb->control);
    uint32_t status = XHCI_READ32(&trb->status);
    uint32_t slot_id = READ_FIELD(control, TRB_SLOT_ID_START, TRB_SLOT_ID_BITS);
    // ep_index is device context index, so decrement by 1 to get zero based index
    uint32_t ep_index = READ_FIELD(control, TRB_ENDPOINT_ID_START, TRB_ENDPOINT_ID_BITS) - 1;
    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep =  &slot->eps[ep_index];
    xhci_transfer_ring_t* ring = &ep->transfer_ring;

    if (!ep->enabled) {
        // endpoint shutting down. device manager thread will complete all pending transations
        return;
    }

    uint32_t cc = READ_FIELD(status, EVT_TRB_CC_START, EVT_TRB_CC_BITS);
    uint32_t length = READ_FIELD(status, EVT_TRB_XFER_LENGTH_START, EVT_TRB_XFER_LENGTH_BITS);
    iotxn_t* txn = NULL;

    // TRB pointer is zero in these cases
    if (cc != TRB_CC_RING_UNDERRUN && cc != TRB_CC_RING_OVERRUN) {
        if (control & EVT_TRB_ED) {
            txn = (iotxn_t *)trb_get_ptr(trb);
        } else {
            trb = xhci_read_trb_ptr(ring, trb);
            for (int i = 0; i < 5 && trb; i++) {
                if (trb_get_type(trb) == TRB_TRANSFER_EVENT_DATA) {
                    txn = (iotxn_t *)trb_get_ptr(trb);
                    break;
                }
                trb = xhci_get_next_trb(ring, trb);
            }
        }
    }

    mx_status_t result;
    switch (cc) {
        case TRB_CC_SUCCESS:
        case TRB_CC_SHORT_PACKET:
            result = length;
            break;
        case TRB_CC_STALL_ERROR:
            result = ERR_IO_REFUSED;
            break;
        case TRB_CC_RING_UNDERRUN:
            // non-fatal error that happens when no transfers are available for isochronous endpoint
            xprintf("TRB_CC_RING_UNDERRUN\n");
            return;
        case TRB_CC_RING_OVERRUN:
            // non-fatal error that happens when no transfers are available for isochronous endpoint
            xprintf("TRB_CC_RING_OVERRUN\n");
            return;
        case TRB_CC_STOPPED:
        case TRB_CC_STOPPED_LENGTH_INVALID:
        case TRB_CC_STOPPED_SHORT_PACKET:
            // for these errors the transfer ring may no longer exist,
            // so it is not safe to attempt to retrieve our transfer context
            xprintf("ignoring transfer event with cc: %d\n", cc);
            return;
        default:
            result = ERR_REMOTE_CLOSED;
            break;
    }

    if (!txn) {
        printf("unable to find iotxn in xhci_handle_transfer_event\n");
        return;
    }

    mtx_lock(&ep->lock);

    // when transaction errors occur, we sometimes receive multiple events for the same transfer.
    // here we check to make sure that this event doesn't correspond to a transfer that has already
    // been completed. In the typical case, the context will be found at the head of pending_requests.
    bool found_txn = false;
    iotxn_t* test;
    list_for_every_entry(&ep->pending_requests, test, iotxn_t, node) {
        if (test == txn) {
            found_txn = true;
            break;
        }
    }
    if (!found_txn) {
        printf("ignoring transfer event for completed transfer\n");
        mtx_unlock(&ep->lock);
        return;
    }

    // update dequeue_ptr to TRB following this transaction
    ring->dequeue_ptr = txn->context;

    // remove txn from pending_requests
    list_delete(&txn->node);

    bool process_deferred_txns = !list_is_empty(&ep->deferred_txns);
    mtx_unlock(&ep->lock);

    if (result < 0) {
        iotxn_complete(txn, result, 0);
    } else {
        iotxn_complete(txn, NO_ERROR, result);
    }

    if (process_deferred_txns) {
        xhci_process_deferred_txns(xhci, ep, false);
    }
}
