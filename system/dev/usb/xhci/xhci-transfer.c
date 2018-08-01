// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb-hci.h>
#include <zircon/assert.h>
#include <zircon/hw/usb.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "xhci-transfer.h"
#include "xhci-util.h"

// reads a range of bits from an integer
#define READ_FIELD(i, start, bits) (((i) >> (start)) & ((1 << (bits)) - 1))

// This resets the transfer ring's dequeue pointer just past the last completed transfer.
// This can only be called when the endpoint is stopped and we are locked on ep->lock.
static zx_status_t xhci_reset_dequeue_ptr_locked(xhci_t* xhci, uint32_t slot_id,
                                                 uint32_t ep_index) {
    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    xhci_transfer_ring_t* transfer_ring = &ep->transfer_ring;

    xhci_sync_command_t command;
    xhci_sync_command_init(&command);
    uint64_t ptr = xhci_transfer_ring_current_phys(transfer_ring);
    ptr |= transfer_ring->pcs;
    // command expects device context index, so increment ep_index by 1
    uint32_t control = (slot_id << TRB_SLOT_ID_START) |
                        ((ep_index + 1) << TRB_ENDPOINT_ID_START);
    xhci_post_command(xhci, TRB_CMD_SET_TR_DEQUEUE, ptr, control, &command.context);
    int cc = xhci_sync_command_wait(&command);
    if (cc != TRB_CC_SUCCESS) {
        zxlogf(ERROR, "TRB_CMD_SET_TR_DEQUEUE failed cc: %d\n", cc);
        return ZX_ERR_INTERNAL;
    }
    xhci_set_dequeue_ptr(transfer_ring, transfer_ring->current);

    return ZX_OK;
}

static void xhci_process_transactions_locked(xhci_t* xhci, xhci_slot_t* slot, uint8_t ep_index,
                                             list_node_t* completed_reqs);

zx_status_t xhci_reset_endpoint(xhci_t* xhci, uint32_t slot_id, uint8_t ep_address) {
    xhci_slot_t* slot = &xhci->slots[slot_id];
    uint8_t ep_index = xhci_endpoint_index(ep_address);
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    usb_request_t* req;

    // Recover from Halted and Error conditions. See section 4.8.3 of the XHCI spec.

    mtx_lock(&ep->lock);

    if (ep->state != EP_STATE_HALTED && ep->state != EP_STATE_ERROR) {
        mtx_unlock(&ep->lock);
        return ZX_OK;
    }

    int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
    zxlogf(TRACE, "xhci_reset_endpoint %d %d ep_ctx_state %d\n", slot_id, ep_index, ep_ctx_state);

    if (ep_ctx_state == EP_CTX_STATE_STOPPED || ep_ctx_state == EP_CTX_STATE_RUNNING) {
        ep->state = EP_STATE_RUNNING;
        mtx_unlock(&ep->lock);
        return ZX_OK;
    }
    if (ep_ctx_state == EP_CTX_STATE_HALTED) {
        // reset the endpoint to move from Halted to Stopped state
        xhci_sync_command_t command;
        xhci_sync_command_init(&command);
        // command expects device context index, so increment ep_index by 1
        uint32_t control = (slot_id << TRB_SLOT_ID_START) |
                            ((ep_index + 1) << TRB_ENDPOINT_ID_START);
        xhci_post_command(xhci, TRB_CMD_RESET_ENDPOINT, 0, control, &command.context);
        // Release the lock before waiting for the command. The command may not complete,
        // if there is another transfer event on the completer thread waiting for the lock
        // on the same endpoint.
        mtx_unlock(&ep->lock);
        int cc = xhci_sync_command_wait(&command);
        if (cc != TRB_CC_SUCCESS) {
            zxlogf(ERROR, "xhci_reset_endpoint: TRB_CMD_RESET_ENDPOINT failed cc: %d\n", cc);
            return ZX_ERR_INTERNAL;
        }
        mtx_lock(&ep->lock);

        // calling USB_REQ_CLEAR_FEATURE on a stalled control endpoint will not work,
        // so we only do this for the other endpoints
        if (ep_address != 0) {
            // This should come after the successful completion of a Reset Endpoint Command.
            // See XHCI spec, section 4.6.8
            xhci_control_request(xhci, slot_id, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT,
                                 USB_REQ_CLEAR_FEATURE, USB_ENDPOINT_HALT, ep_address, NULL, 0);
        }
    }

    // resetting the dequeue pointer gets us out of ERROR state, and is also necessary
    // after TRB_CMD_RESET_ENDPOINT.
    if (ep_ctx_state == EP_CTX_STATE_ERROR || ep_ctx_state == EP_CTX_STATE_HALTED) {
        // move transfer ring's dequeue pointer passed the failed transaction
        zx_status_t status = xhci_reset_dequeue_ptr_locked(xhci, slot_id, ep_index);
        if (status != ZX_OK) {
            mtx_unlock(&ep->lock);
            return status;
        }
    }

    // xhci_reset_dequeue_ptr_locked will skip past all pending transactions,
    // so move them all to the queued list so they will be requeued
    // Completed these with ZX_ERR_CANCELED out of the lock.
    // Remove from tail and add to head to preserve the ordering
    while ((req = list_remove_tail_type(&ep->pending_reqs, usb_request_t, node)) != NULL) {
        list_add_head(&ep->queued_reqs, &req->node);
    }

    ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
    zx_status_t status;
    switch (ep_ctx_state) {
    case EP_CTX_STATE_DISABLED:
        ep->state = EP_STATE_DEAD;
        status = ZX_ERR_IO_NOT_PRESENT;
        break;
    case EP_CTX_STATE_RUNNING:
    case EP_CTX_STATE_STOPPED:
        ep->state = EP_STATE_RUNNING;
        status = ZX_OK;
        break;
    case EP_CTX_STATE_ERROR:
        ep->state = EP_STATE_ERROR;
        status = ZX_ERR_IO_INVALID;
        break;
    case EP_CTX_STATE_HALTED:
        ep->state = EP_STATE_HALTED;
        status = ZX_ERR_IO_REFUSED;
        break;
    default:
        ep->state = EP_STATE_HALTED;
        status = ZX_ERR_INTERNAL;
        break;
    }

    list_node_t completed_reqs = LIST_INITIAL_VALUE(completed_reqs);
    if (ep->state == EP_STATE_RUNNING) {
        // start processing transactions again
        xhci_process_transactions_locked(xhci, slot, ep_index, &completed_reqs);
    }

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((req = list_remove_head_type(&completed_reqs, usb_request_t, node)) != NULL) {
        usb_request_complete(req, req->response.status, req->response.actual);
    }

    return status;
}

// locked on ep->lock
static zx_status_t xhci_start_transfer_locked(xhci_t* xhci, xhci_slot_t* slot, uint32_t ep_index,
                                              usb_request_t* req) {
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    xhci_transfer_ring_t* ring = &ep->transfer_ring;
    if (ep->state != EP_STATE_RUNNING) {
        zxlogf(ERROR, "xhci_start_transfer_locked bad ep->state %d\n", ep->state);
        return ZX_ERR_BAD_STATE;
    }

    if (req->header.length > 0) {
        zx_status_t status = usb_request_physmap(req);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: usb_request_physmap failed: %d\n", __FUNCTION__, status);
            return status;
        }
    }

    xhci_transfer_state_t* state = ep->transfer_state;
    xhci_transfer_state_init(state, req, ep->ep_type, ep->max_packet_size);

    size_t length = req->header.length;
    uint32_t interrupter_target = 0;

    usb_setup_t* setup = (req->header.ep_address == 0 ? &req->setup : NULL);
    if (setup) {
        // Setup Stage
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);

        XHCI_SET_BITS32(&trb->ptr_low, SETUP_TRB_REQ_TYPE_START, SETUP_TRB_REQ_TYPE_BITS,
                        setup->bmRequestType);
        XHCI_SET_BITS32(&trb->ptr_low, SETUP_TRB_REQUEST_START, SETUP_TRB_REQUEST_BITS,
                        setup->bRequest);
        XHCI_SET_BITS32(&trb->ptr_low, SETUP_TRB_VALUE_START, SETUP_TRB_VALUE_BITS, setup->wValue);
        XHCI_SET_BITS32(&trb->ptr_high, SETUP_TRB_INDEX_START, SETUP_TRB_INDEX_BITS, setup->wIndex);
        XHCI_SET_BITS32(&trb->ptr_high, SETUP_TRB_LENGTH_START, SETUP_TRB_LENGTH_BITS, length);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_XFER_LENGTH_START, XFER_TRB_XFER_LENGTH_BITS, 8);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS,
                        interrupter_target);

        uint32_t control_bits = (length == 0 ? XFER_TRB_TRT_NONE :
                            (state->direction == USB_DIR_IN ? XFER_TRB_TRT_IN : XFER_TRB_TRT_OUT));
        control_bits |= XFER_TRB_IDT; // immediate data flag
        trb_set_control(trb, TRB_TRANSFER_SETUP, control_bits);
        if (driver_get_log_flags() & DDK_LOG_SPEW) xhci_print_trb(ring, trb);
        xhci_increment_ring(ring);
    }

    return ZX_OK;
}

// returns ZX_OK if req has been successfully queued,
// ZX_ERR_SHOULD_WAIT if we ran out of TRBs and need to try again later,
// or other error for a hard failure.
static zx_status_t xhci_continue_transfer_locked(xhci_t* xhci, xhci_slot_t* slot,
                                                 uint32_t ep_index, usb_request_t* req) {
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    xhci_transfer_ring_t* ring = &ep->transfer_ring;

    usb_header_t* header = &req->header;
    xhci_transfer_state_t* state = ep->transfer_state;
    size_t length = header->length;
    size_t free_trbs = xhci_transfer_ring_free_trbs(&ep->transfer_ring);
    uint8_t direction = state->direction;
    bool isochronous = (ep->ep_type == USB_ENDPOINT_ISOCHRONOUS);
    uint64_t frame = header->frame;

    uint32_t interrupter_target = 0;

    if (isochronous) {
        if (length == 0) return ZX_ERR_INVALID_ARGS;
        if (xhci->num_interrupts > 1) {
            interrupter_target = ISOCH_INTERRUPTER;
        }
    }

    if (frame != 0) {
        if (!isochronous) {
            zxlogf(ERROR, "frame scheduling only supported for isochronous transfers\n");
            return ZX_ERR_INVALID_ARGS;
        }
        uint64_t current_frame = xhci_get_current_frame(xhci);
        if (frame < current_frame) {
            zxlogf(ERROR, "can't schedule transfer into the past\n");
            return ZX_ERR_INVALID_ARGS;
        }
        if (frame - current_frame >= 895) {
            // See XHCI spec, section 4.11.2.5
            zxlogf(ERROR, "can't schedule transfer more than 895ms into the future\n");
            return ZX_ERR_INVALID_ARGS;
        }
    }

    // need to clean the cache for both IN and OUT transfers, invalidate only for IN
    if (direction == USB_DIR_IN) {
        usb_request_cache_flush_invalidate(req, 0, header->length);
    } else {
        usb_request_cache_flush(req, 0, header->length);
    }

    zx_status_t status = xhci_queue_data_trbs(ring, state, req, interrupter_target, isochronous);
    if (status != ZX_OK) {
        return status;
    }

    if (state->needs_status) {
        if (free_trbs == 0) {
            // will need to do this later
            return ZX_ERR_SHOULD_WAIT;
        }

        // Status Stage
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS,
                        interrupter_target);
        uint32_t control_bits = (direction == USB_DIR_IN && length > 0 ? XFER_TRB_DIR_OUT
                                                                       : XFER_TRB_DIR_IN);
        // generate an event for the status phase so we can catch stalls or other errors
        // before completing control transfer requests
        control_bits |= XFER_TRB_IOC;
        trb_set_control(trb, TRB_TRANSFER_STATUS, control_bits);
        if (driver_get_log_flags() & DDK_LOG_SPEW) xhci_print_trb(ring, trb);
        xhci_increment_ring(ring);
        free_trbs--;
        state->needs_status = false;
    }

    // if we get here, then we are ready to ring the doorbell
    // update dequeue_ptr to TRB following this transaction
    req->context = (void *)ring->current;

    XHCI_WRITE32(&xhci->doorbells[header->device_id], ep_index + 1);
    // it seems we need to ring the doorbell a second time when transitioning from STOPPED
    while (xhci_get_ep_ctx_state(slot, ep) == EP_CTX_STATE_STOPPED) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
        XHCI_WRITE32(&xhci->doorbells[header->device_id], ep_index + 1);
    }

    return ZX_OK;
}

static void xhci_process_transactions_locked(xhci_t* xhci, xhci_slot_t* slot, uint8_t ep_index,
                                             list_node_t* completed_reqs) {
    xhci_endpoint_t* ep = &slot->eps[ep_index];

    // loop until we fill our transfer ring or run out of requests to process
    while (1) {
        if (xhci_transfer_ring_free_trbs(&ep->transfer_ring) == 0) {
            // no available TRBs - need to wait for some complete
            return;
        }

        while (!ep->current_req) {
            // start the next transaction in the queue
            usb_request_t* req = list_remove_head_type(&ep->queued_reqs, usb_request_t, node);
            if (!req) {
                // nothing to do
                return;
            }

            zx_status_t status = xhci_start_transfer_locked(xhci, slot, ep_index, req);
            if (status == ZX_OK) {
                list_add_tail(&ep->pending_reqs, &req->node);
                ep->current_req = req;
            } else {
                req->response.status = status;
                req->response.actual = 0;
                list_add_tail(completed_reqs, &req->node);
            }
        }

        if (ep->current_req) {
            usb_request_t* req = ep->current_req;
            zx_status_t status = xhci_continue_transfer_locked(xhci, slot, ep_index, req);
            if (status == ZX_ERR_SHOULD_WAIT) {
                // no available TRBs - need to wait for some complete
                return;
            } else {
                if (status != ZX_OK) {
                    req->response.status = status;
                    req->response.actual = 0;
                    list_delete(&req->node);
                    list_add_tail(completed_reqs, &req->node);
                }
                ep->current_req = NULL;
            }
        }
    }
}

zx_status_t xhci_queue_transfer(xhci_t* xhci, usb_request_t* req) {
    uint32_t slot_id = req->header.device_id;
    uint8_t ep_index = xhci_endpoint_index(req->header.ep_address);
    __UNUSED usb_setup_t* setup = (ep_index == 0 ? &req->setup : NULL);

    zxlogf(LSPEW, "xhci_queue_transfer slot_id: %d setup: %p ep_index: %d length: %lu\n",
            slot_id, setup, ep_index, req->header.length);

    int rh_index = xhci_get_root_hub_index(xhci, slot_id);
    if (rh_index >= 0) {
        return xhci_rh_usb_request_queue(xhci, req, rh_index);
    }

    if (slot_id < 1 || slot_id > xhci->max_slots) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (ep_index >= XHCI_NUM_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }

    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    if (!slot->sc) {
        // slot no longer enabled
        return ZX_ERR_IO_NOT_PRESENT;
    }

    mtx_lock(&ep->lock);

    zx_status_t status;
    switch (ep->state) {
    case EP_STATE_DEAD:
        status = ZX_ERR_IO_NOT_PRESENT;
        break;
    case EP_STATE_RUNNING:
        status = ZX_OK;
        break;
    case EP_STATE_PAUSED:
        status = ZX_ERR_BAD_STATE;
        break;
    case EP_STATE_ERROR:
        status = ZX_ERR_IO_INVALID;
        break;
    case EP_STATE_HALTED:
        status = ZX_ERR_IO_REFUSED;
        break;
    case EP_STATE_DISABLED:
        status = ZX_ERR_BAD_STATE;
        break;
    default:
        status = ZX_ERR_INTERNAL;
        break;
    }

    if (status != ZX_OK) {
        mtx_unlock(&ep->lock);
        return status;
    }

    list_add_tail(&ep->queued_reqs, &req->node);

    list_node_t completed_reqs = LIST_INITIAL_VALUE(completed_reqs);
    xhci_process_transactions_locked(xhci, slot, ep_index, &completed_reqs);

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((req = list_remove_head_type(&completed_reqs, usb_request_t, node)) != NULL) {
        usb_request_complete(req, req->response.status, req->response.actual);
    }

    return ZX_OK;
}

zx_status_t xhci_cancel_transfers(xhci_t* xhci, uint32_t slot_id, uint32_t ep_index) {
    zxlogf(TRACE, "xhci_cancel_transfers slot_id: %d ep_index: %d\n", slot_id, ep_index);

    if (slot_id < 1 || slot_id > xhci->max_slots) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (ep_index >= XHCI_NUM_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }

    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    list_node_t completed_reqs = LIST_INITIAL_VALUE(completed_reqs);
    usb_request_t* req;
    usb_request_t* temp;
    zx_status_t status = ZX_OK;

    mtx_lock(&ep->lock);

    if (ep->state == EP_STATE_HALTED) {
      // xhci_reset_endpoint will be issued, when the transaction
      // that caused the STALL is completed. Let xhci_reset_endpoint
      // take care of resetting the endpoint to a running state.
      mtx_unlock(&ep->lock);
      return status;
    }
    if (!list_is_empty(&ep->pending_reqs)) {
        // stop the endpoint and remove transactions that have already been queued
        // in the transfer ring
        ep->state = EP_STATE_PAUSED;

        xhci_sync_command_t command;
        xhci_sync_command_init(&command);
        // command expects device context index, so increment ep_index by 1
        uint32_t control = (slot_id << TRB_SLOT_ID_START) |
                           ((ep_index + 1) << TRB_ENDPOINT_ID_START);
        xhci_post_command(xhci, TRB_CMD_STOP_ENDPOINT, 0, control, &command.context);

        // We can't block on command completion while holding the lock.
        // It is safe to unlock here because no additional transactions will be
        // queued on the endpoint when ep->state is EP_STATE_PAUSED.
        mtx_unlock(&ep->lock);
        int cc = xhci_sync_command_wait(&command);
        if (cc != TRB_CC_SUCCESS) {
            // TRB_CC_CONTEXT_STATE_ERROR is normal here in the case of a disconnected device,
            // since by then the endpoint would already be in error state.
            zxlogf(ERROR, "xhci_cancel_transfers: TRB_CMD_STOP_ENDPOINT failed cc: %d\n", cc);
            return ZX_ERR_INTERNAL;
        }
        mtx_lock(&ep->lock);

        // TRB_CMD_STOP_ENDPOINT may have have completed a currently executing request
        // but we may still have other pending requests. xhci_reset_dequeue_ptr_locked()
        // will set the dequeue pointer after the last completed request.
        list_for_every_entry_safe(&ep->pending_reqs, req, temp, usb_request_t, node) {
            list_delete(&req->node);
            req->response.status = ZX_ERR_CANCELED;
            req->response.actual = 0;
            list_add_head(&completed_reqs, &req->node);
        }

        status = xhci_reset_dequeue_ptr_locked(xhci, slot_id, ep_index);
        if (status == ZX_OK) {
            ep->state = EP_STATE_RUNNING;
        }
    }

    // elements of the queued_reqs list can simply be removed and completed.
    list_for_every_entry_safe(&ep->queued_reqs, req, temp, usb_request_t, node) {
        list_delete(&req->node);
        req->response.status = ZX_ERR_CANCELED;
        req->response.actual = 0;
        list_add_head(&completed_reqs, &req->node);
    }

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((req = list_remove_head_type(&completed_reqs, usb_request_t, node)) != NULL) {
        usb_request_complete(req, req->response.status, req->response.actual);
    }

    return status;
}

static void xhci_control_complete(usb_request_t* req, void* cookie) {
    sync_completion_signal((sync_completion_t*)cookie);
}

int xhci_control_request(xhci_t* xhci, uint32_t slot_id, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void* data, uint16_t length) {

    zxlogf(LTRACE, "xhci_control_request slot_id: %d type: 0x%02X req: %d value: %d index: %d "
            "length: %d\n", slot_id, request_type, request, value, index, length);

    // xhci_control_request is only used for reading first 8 bytes of the device descriptor,
    // so it makes sense to pool them.
    usb_request_t* req = usb_request_pool_get(&xhci->free_reqs, length);

    if (req == NULL) {
        zx_status_t status = usb_request_alloc(&req, xhci->bti_handle, length, 0);
        if (status != ZX_OK) return status;
    }

    usb_setup_t* setup = &req->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
    req->header.device_id = slot_id;

    bool out = !!((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (length > 0 && out) {
        usb_request_copyto(req, data, length, 0);
    }

    sync_completion_t completion = SYNC_COMPLETION_INIT;

    req->header.length = length;
    req->complete_cb = xhci_control_complete;
    req->cookie = &completion;
    xhci_request_queue(xhci, req);
    zx_status_t status = sync_completion_wait(&completion, ZX_SEC(1));
    if (status == ZX_OK) {
        status = req->response.status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        zxlogf(ERROR, "xhci_control_request ZX_ERR_TIMED_OUT\n");
        sync_completion_reset(&completion);
        status = xhci_cancel_transfers(xhci, slot_id, 0);
        if (status == ZX_OK) {
            sync_completion_wait(&completion, ZX_TIME_INFINITE);
            status = ZX_ERR_TIMED_OUT;
        }
    }
    zxlogf(TRACE, "xhci_control_transfer got %d\n", status);
    if (status == ZX_OK) {
        status = req->response.actual;

        if (length > 0 && !out) {
            usb_request_copyfrom(req, data, req->response.actual, 0);
        }
    }

    usb_request_pool_add(&xhci->free_reqs, req);

    zxlogf(TRACE, "xhci_control_request returning %d\n", status);
    return status;
}

zx_status_t xhci_get_descriptor(xhci_t* xhci, uint32_t slot_id, uint8_t type, uint16_t value,
                                uint16_t index, void* data, uint16_t length) {
    return xhci_control_request(xhci, slot_id, USB_DIR_IN | type | USB_RECIP_DEVICE,
                                USB_REQ_GET_DESCRIPTOR, value, index, data, length);
}

void xhci_handle_transfer_event(xhci_t* xhci, xhci_trb_t* trb) {
    zxlogf(LTRACE, "xhci_handle_transfer_event: %08X %08X %08X %08X\n",
            ((uint32_t*)trb)[0], ((uint32_t*)trb)[1], ((uint32_t*)trb)[2], ((uint32_t*)trb)[3]);

    uint32_t control = XHCI_READ32(&trb->control);
    uint32_t status = XHCI_READ32(&trb->status);
    uint32_t slot_id = READ_FIELD(control, TRB_SLOT_ID_START, TRB_SLOT_ID_BITS);
    // ep_index is device context index, so decrement by 1 to get zero based index
    uint32_t ep_index = READ_FIELD(control, TRB_ENDPOINT_ID_START, TRB_ENDPOINT_ID_BITS) - 1;
    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    xhci_transfer_ring_t* ring = &ep->transfer_ring;

    uint32_t cc = READ_FIELD(status, EVT_TRB_CC_START, EVT_TRB_CC_BITS);
    uint32_t length = READ_FIELD(status, EVT_TRB_XFER_LENGTH_START, EVT_TRB_XFER_LENGTH_BITS);
    usb_request_t* req = NULL;

    mtx_lock(&ep->lock);

    zx_status_t result;
    switch (cc) {
        case TRB_CC_SUCCESS:
        case TRB_CC_SHORT_PACKET:
            result = length;
            break;
        case TRB_CC_BABBLE_DETECTED_ERROR:
            zxlogf(TRACE, "xhci_handle_transfer_event: TRB_CC_BABBLE_DETECTED_ERROR\n");
            result = ZX_ERR_IO_OVERRUN;
            break;
        case TRB_CC_TRB_ERROR:
            zxlogf(TRACE, "xhci_handle_transfer_event: TRB_CC_TRB_ERROR\n");
            int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
            /*
             * For usb-c ethernet adapters on Intel xhci controller, we receive this error
             * when a packet fails with NRDY token on the bus.see NET:97 for more details.
             * Slow down the requests in the client when this error is received.
             */
            if (ep_ctx_state == EP_CTX_STATE_ERROR) {
                result = ZX_ERR_IO_INVALID;
            } else {
                result = ZX_ERR_IO;
            }
            break;
        case TRB_CC_USB_TRANSACTION_ERROR:
        case TRB_CC_STALL_ERROR: {
            int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
            zxlogf(TRACE, "xhci_handle_transfer_event: cc %d ep_ctx_state %d\n", cc, ep_ctx_state);
            if (ep_ctx_state == EP_CTX_STATE_HALTED) {
                result = ZX_ERR_IO_REFUSED;
            } else {
                result = ZX_ERR_IO;
            }
            break;
        }
        case TRB_CC_RING_UNDERRUN:
            // non-fatal error that happens when no transfers are available for isochronous endpoint
            zxlogf(TRACE, "xhci_handle_transfer_event: TRB_CC_RING_UNDERRUN\n");
            mtx_unlock(&ep->lock);
            return;
        case TRB_CC_RING_OVERRUN:
            // non-fatal error that happens when no transfers are available for isochronous endpoint
            zxlogf(TRACE, "xhci_handle_transfer_event: TRB_CC_RING_OVERRUN\n");
            mtx_unlock(&ep->lock);
            return;
       case TRB_CC_MISSED_SERVICE_ERROR:
            zxlogf(TRACE, "xhci_handle_transfer_event: TRB_CC_MISSED_SERVICE_ERROR\n");
            result = ZX_ERR_IO_MISSED_DEADLINE;
            break;
        case TRB_CC_STOPPED:
        case TRB_CC_STOPPED_LENGTH_INVALID:
        case TRB_CC_STOPPED_SHORT_PACKET:
        case TRB_CC_ENDPOINT_NOT_ENABLED_ERROR:
            switch (ep->state) {
            case EP_STATE_PAUSED:
                result = ZX_ERR_CANCELED;
                break;
            case EP_STATE_DISABLED:
                result = ZX_ERR_BAD_STATE;
                break;
            case EP_STATE_DEAD:
                result = ZX_ERR_IO_NOT_PRESENT;
                break;
            default:
                zxlogf(ERROR, "xhci_handle_transfer_event: bad state for stopped req: %d\n", ep->state);
                result = ZX_ERR_INTERNAL;
            }
            break;
        default: {
            int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
            zxlogf(ERROR, "xhci_handle_transfer_event: unhandled transfer event condition code %d "
                   "ep_ctx_state %d:  %08X %08X %08X %08X\n", cc, ep_ctx_state,
                    ((uint32_t*)trb)[0], ((uint32_t*)trb)[1], ((uint32_t*)trb)[2], ((uint32_t*)trb)[3]);
            if (ep_ctx_state == EP_CTX_STATE_HALTED) {
                result = ZX_ERR_IO_REFUSED;
            } else if (ep_ctx_state == EP_CTX_STATE_ERROR) {
                result = ZX_ERR_IO_INVALID;
            } else {
                result = ZX_ERR_IO;
            }
            break;
        }
    }

    bool req_status_set = false;

    if (trb_get_ptr(trb) && !list_is_empty(&ep->pending_reqs)) {
        if (control & EVT_TRB_ED) {
            req = (usb_request_t *)trb_get_ptr(trb);
            if (ep_index == 0) {
                // For control requests we are expecting a second transfer event to signal the end
                // of the status phase. So here we record the status and actual for the data phase
                // but wait for the status phase to complete before completing the request.
                slot->current_ctrl_req = req;
                if (result < 0) {
                    req->response.status = result;
                    req->response.actual = 0;
                } else {
                    req->response.status = 0;
                    req->response.actual = result;
                }
                mtx_unlock(&ep->lock);
                return;
            }
        } else {
            trb = xhci_read_trb_ptr(ring, trb);
            if (trb_get_type(trb) == TRB_TRANSFER_STATUS && slot->current_ctrl_req) {
                // complete current control request
                req = slot->current_ctrl_req;
                slot->current_ctrl_req = NULL;
                if (result < 0) {
                    // sometimes we receive stall errors in the status phase so update
                    // request status if necessary
                    req->response.status = result;
                    req->response.actual = 0;
                }
                req_status_set = true;
            } else {
                for (uint i = 0; i < TRANSFER_RING_SIZE && trb; i++) {
                    if (trb_get_type(trb) == TRB_TRANSFER_EVENT_DATA) {
                        req = (usb_request_t *)trb_get_ptr(trb);
                        break;
                    }
                    trb = xhci_get_next_trb(ring, trb);
                }
            }
        }
    }

    int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
    if (ep_ctx_state != EP_CTX_STATE_RUNNING) {
        zxlogf(TRACE, "xhci_handle_transfer_event: ep ep_ctx_state %d cc %d\n", ep_ctx_state, cc);
    }

    if (!req) {
        // no req expected for this condition code
        if (cc != TRB_CC_STOPPED_LENGTH_INVALID) {
            zxlogf(TRACE, "xhci_handle_transfer_event: unable to find request to complete!\n");
        }
        mtx_unlock(&ep->lock);
        return;
    }

    // when transaction errors occur, we sometimes receive multiple events for the same transfer.
    // here we check to make sure that this event doesn't correspond to a transfer that has already
    // been completed. In the typical case, the context will be found at the head of pending_reqs.
    bool found_req = false;
    usb_request_t* test;
    list_for_every_entry(&ep->pending_reqs, test, usb_request_t, node) {
        if (test == req) {
            found_req = true;
            break;
        }
    }
    if (!found_req) {
        zxlogf(TRACE, "xhci_handle_transfer_event: ignoring transfer event for completed transfer\n");
        mtx_unlock(&ep->lock);
        return;
    }

    // update dequeue_ptr to TRB following this transaction
    xhci_set_dequeue_ptr(ring, req->context);

    // remove request from pending_reqs
    list_delete(&req->node);

    if (!req_status_set) {
        if (result < 0) {
            req->response.status = result;
            req->response.actual = 0;
        } else {
            req->response.status = 0;
            req->response.actual = result;
        }
    }

    list_node_t completed_reqs = LIST_INITIAL_VALUE(completed_reqs);
    list_add_head(&completed_reqs, &req->node);

    if (result == ZX_ERR_IO_REFUSED && ep->state != EP_STATE_DEAD) {
        ep->state = EP_STATE_HALTED;
    } else if (result == ZX_ERR_IO_INVALID && ep->state != EP_STATE_DEAD) {
        ep->state = EP_STATE_ERROR;
    } else if (ep->state == EP_STATE_RUNNING) {
        xhci_process_transactions_locked(xhci, slot, ep_index, &completed_reqs);
    }

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((req = list_remove_head_type(&completed_reqs, usb_request_t, node)) != NULL) {
        usb_request_complete(req, req->response.status, req->response.actual);
    }
}
