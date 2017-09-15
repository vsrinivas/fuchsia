// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/usb.h>
#include <zircon/assert.h>
#include <zircon/hw/usb.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "xhci-transfer.h"
#include "xhci-util.h"

static void print_trb(xhci_t* xhci, xhci_transfer_ring_t* ring, xhci_trb_t* trb) {
    int index = trb - ring->start;
    uint32_t* ptr = (uint32_t *)trb;
    uint64_t paddr = io_buffer_phys(&ring->buffer) + index * sizeof(xhci_trb_t);

    dprintf(LSPEW, "trb[%03d] %p: %08X %08X %08X %08X\n", index, (void *)paddr, ptr[0], ptr[1], ptr[2], ptr[3]);
}

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
        dprintf(ERROR, "TRB_CMD_SET_TR_DEQUEUE failed cc: %d\n", cc);
        return ZX_ERR_INTERNAL;
    }
    transfer_ring->dequeue_ptr = transfer_ring->current;

    return ZX_OK;
}

static void xhci_process_transactions_locked(xhci_t* xhci, xhci_slot_t* slot, uint8_t ep_index,
                                             list_node_t* completed_txns);

zx_status_t xhci_reset_endpoint(xhci_t* xhci, uint32_t slot_id, uint32_t ep_index) {
    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    iotxn_t* txn;

    // Recover from Halted and Error conditions. See section 4.8.3 of the XHCI spec.

    mtx_lock(&ep->lock);

    if (ep->state != EP_STATE_HALTED) {
        mtx_unlock(&ep->lock);
        return ZX_OK;
    }

    int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
    dprintf(TRACE, "xhci_reset_endpoint %d %d ep_ctx_state %d\n", slot_id, ep_index, ep_ctx_state);

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
        int cc = xhci_sync_command_wait(&command);
        if (cc != TRB_CC_SUCCESS) {
            dprintf(ERROR, "xhci_reset_endpoint: TRB_CMD_RESET_ENDPOINT failed cc: %d\n", cc);
            mtx_unlock(&ep->lock);
            return ZX_ERR_INTERNAL;
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
    while ((txn = list_remove_tail_type(&ep->pending_txns, iotxn_t, node)) != NULL) {
        list_add_head(&ep->queued_txns, &txn->node);
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
    case EP_CTX_STATE_HALTED:
    case EP_CTX_STATE_ERROR:
        ep->state = EP_STATE_HALTED;
        status = ZX_ERR_IO_REFUSED;
        break;
    default:
        ep->state = EP_STATE_HALTED;
        status = ZX_ERR_INTERNAL;
        break;
    }

    list_node_t completed_txns = LIST_INITIAL_VALUE(completed_txns);
    if (ep->state == EP_STATE_RUNNING) {
        // start processing transactions again
        xhci_process_transactions_locked(xhci, slot, ep_index, &completed_txns);
    }

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((txn = list_remove_head_type(&completed_txns, iotxn_t, node)) != NULL) {
        iotxn_complete(txn, txn->status, txn->actual);
    }

    return status;
}

// locked on ep->lock
static zx_status_t xhci_start_transfer_locked(xhci_t* xhci, xhci_slot_t* slot, uint32_t ep_index,
                                              iotxn_t* txn) {
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    xhci_transfer_ring_t* ring = &ep->transfer_ring;
    if (ep->state != EP_STATE_RUNNING) {
        dprintf(ERROR, "xhci_start_transfer_locked bad ep->state %d\n", ep->state);
        return ZX_ERR_BAD_STATE;
    }

    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);
    xhci_transfer_state_t* state = ep->transfer_state;
    memset(state, 0, sizeof(*state));

    if (txn->length > 0) {
        zx_status_t status = iotxn_physmap(txn);
        if (status != ZX_OK) {
            dprintf(ERROR, "%s: iotxn_physmap failed: %d\n", __FUNCTION__, status);
            return status;
        }
    }

    // compute number of packets needed for this transaction
    if (txn->length > 0) {
        iotxn_phys_iter_init(&state->phys_iter, txn, XHCI_MAX_DATA_BUFFER);
        zx_paddr_t dummy_paddr;
        while (iotxn_phys_iter_next(&state->phys_iter, &dummy_paddr) > 0) {
            state->packet_count++;
        }
    }

    iotxn_phys_iter_init(&state->phys_iter, txn, XHCI_MAX_DATA_BUFFER);
    const xhci_endpoint_context_t* epc = ep->epc;
#if XHCI_USE_CACHE_OPS
    io_buffer_cache_op(&slot->buffer, ZX_VMO_OP_CACHE_INVALIDATE,
                       (ep_index + 1) * xhci->context_size, sizeof(xhci_endpoint_context_t));
#endif
    uint32_t ep_type = XHCI_GET_BITS32(&epc->epc1, EP_CTX_EP_TYPE_START, EP_CTX_EP_TYPE_BITS);
    if (ep_type >= 4) ep_type -= 4;
    state->ep_type = ep_type;
    usb_setup_t* setup = (proto_data->ep_address == 0 ? &proto_data->setup : NULL);
    if (setup) {
        state->direction = setup->bmRequestType & USB_ENDPOINT_DIR_MASK;
        state->needs_status = true;
    } else {
        state->direction = proto_data->ep_address & USB_ENDPOINT_DIR_MASK;
    }
    state->needs_data_event = true;
    // Zero length bulk transfers are allowed. We should have at least one transfer TRB
    // to avoid consecutive event data TRBs on a transfer ring.
    // See XHCI spec, section 4.11.5.2
    state->needs_transfer_trb = state->ep_type == USB_ENDPOINT_BULK;

    size_t length = txn->length;
    uint32_t interrupter_target = 0;

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
        if (driver_get_log_flags() & DDK_LOG_SPEW) print_trb(xhci, ring, trb);
        xhci_increment_ring(ring);
    }

    return ZX_OK;
}

// returns ZX_OK if txn has been successfully queued,
// ZX_ERR_SHOULD_WAIT if we ran out of TRBs and need to try again later,
// or other error for a hard failure.
static zx_status_t xhci_continue_transfer_locked(xhci_t* xhci, xhci_slot_t* slot,
                                                 uint32_t ep_index, iotxn_t* txn) {
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    xhci_transfer_ring_t* ring = &ep->transfer_ring;

    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);
    xhci_transfer_state_t* state = ep->transfer_state;
    size_t length = txn->length;
    size_t free_trbs = xhci_transfer_ring_free_trbs(&ep->transfer_ring);
    uint8_t direction = state->direction;
    bool isochronous = (state->ep_type == USB_ENDPOINT_ISOCHRONOUS);
    uint64_t frame = proto_data->frame;

    uint32_t interrupter_target = 0;

    if (isochronous) {
        if (length == 0) return ZX_ERR_INVALID_ARGS;
        if (xhci->num_interrupts > 1) {
            interrupter_target = ISOCH_INTERRUPTER;
        }
    }

    if (frame != 0) {
        if (!isochronous) {
            dprintf(ERROR, "frame scheduling only supported for isochronous transfers\n");
            return ZX_ERR_INVALID_ARGS;
        }
        uint64_t current_frame = xhci_get_current_frame(xhci);
        if (frame < current_frame) {
            dprintf(ERROR, "can't schedule transfer into the past\n");
            return ZX_ERR_INVALID_ARGS;
        }
        if (frame - current_frame >= 895) {
            // See XHCI spec, section 4.11.2.5
            dprintf(ERROR, "can't schedule transfer more than 895ms into the future\n");
            return ZX_ERR_INVALID_ARGS;
        }
    }

#if XHCI_USE_CACHE_OPS
    // need to clean the cache for both IN and OUT transfers
    iotxn_cacheop(txn, IOTXN_CACHE_CLEAN, 0, txn->length);
#endif

    // Data Stage
    zx_paddr_t paddr;
    size_t transfer_size = 0;
    bool first_packet = (state->phys_iter.offset == 0);
    while (free_trbs > 0 && (((transfer_size = iotxn_phys_iter_next(&state->phys_iter, &paddr)) > 0) ||
                             state->needs_transfer_trb)) {
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        XHCI_WRITE64(&trb->ptr, paddr);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_XFER_LENGTH_START, XFER_TRB_XFER_LENGTH_BITS,
                        transfer_size);
        // number of packets remaining after this one
        uint32_t td_size = --state->packet_count;
        XHCI_SET_BITS32(&trb->status, XFER_TRB_TD_SIZE_START, XFER_TRB_TD_SIZE_BITS, td_size);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS,
                        interrupter_target);

        uint32_t control_bits = TRB_CHAIN;
        if (td_size == 0) {
            control_bits |= XFER_TRB_ENT;
        }
        if (ep_index == 0 && first_packet) {
            // use TRB_TRANSFER_DATA for first data packet on setup requests
            control_bits |= (direction == USB_DIR_IN ? XFER_TRB_DIR_IN : XFER_TRB_DIR_OUT);
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
        if (driver_get_log_flags() & DDK_LOG_SPEW) print_trb(xhci, ring, trb);
        xhci_increment_ring(ring);
        free_trbs--;

        first_packet = false;
        state->needs_transfer_trb = false;
    }

    if (state->phys_iter.offset < txn->length) {
        // still more data to queue, but we are out of TRBs.
        // come back and finish later.
        return ZX_ERR_SHOULD_WAIT;
    }

    // if data length is zero, we queue event data after the status TRB
    if (state->needs_data_event && txn->length > 0) {
        if (free_trbs == 0) {
            // will need to do this later
            return ZX_ERR_SHOULD_WAIT;
        }

        // Queue event data TRB
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        trb_set_ptr(trb, txn);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS,
                        interrupter_target);
        trb_set_control(trb, TRB_TRANSFER_EVENT_DATA, XFER_TRB_IOC);
        if (driver_get_log_flags() & DDK_LOG_SPEW) print_trb(xhci, ring, trb);
        xhci_increment_ring(ring);
        free_trbs--;
        state->needs_data_event = false;
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
        if (length == 0) {
            control_bits |= TRB_CHAIN;
        }
        trb_set_control(trb, TRB_TRANSFER_STATUS, control_bits);
        if (driver_get_log_flags() & DDK_LOG_SPEW) print_trb(xhci, ring, trb);
        xhci_increment_ring(ring);
        free_trbs--;
        state->needs_status = false;
    }

     // if data length is zero, we queue event data after the status TRB
    if (state->needs_data_event && txn->length == 0) {
        if (free_trbs == 0) {
            // will need to do this later
            return ZX_ERR_SHOULD_WAIT;
        }

        // Queue event data TRB
        xhci_trb_t* trb = ring->current;
        xhci_clear_trb(trb);
        trb_set_ptr(trb, txn);
        XHCI_SET_BITS32(&trb->status, XFER_TRB_INTR_TARGET_START, XFER_TRB_INTR_TARGET_BITS,
                        interrupter_target);
        trb_set_control(trb, TRB_TRANSFER_EVENT_DATA, XFER_TRB_IOC);
        if (driver_get_log_flags() & DDK_LOG_SPEW) print_trb(xhci, ring, trb);
        xhci_increment_ring(ring);
        free_trbs--;
        state->needs_data_event = false;
    }

    // if we get here, then we are ready to ring the doorbell
    // update dequeue_ptr to TRB following this transaction
    txn->context = (void *)ring->current;

    XHCI_WRITE32(&xhci->doorbells[proto_data->device_id], ep_index + 1);
    // it seems we need to ring the doorbell a second time when transitioning from STOPPED
    while (xhci_get_ep_ctx_state(slot, ep) == EP_CTX_STATE_STOPPED) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
        XHCI_WRITE32(&xhci->doorbells[proto_data->device_id], ep_index + 1);
    }

    return ZX_OK;
}

static void xhci_process_transactions_locked(xhci_t* xhci, xhci_slot_t* slot, uint8_t ep_index,
                                             list_node_t* completed_txns) {
    xhci_endpoint_t* ep = &slot->eps[ep_index];

    // loop until we fill our transfer ring or run out of iotxns to process
    while (1) {
        if (xhci_transfer_ring_free_trbs(&ep->transfer_ring) == 0) {
            // no available TRBs - need to wait for some complete
            return;
        }

        while (!ep->current_txn) {
            // start the next transaction in the queue
            iotxn_t* txn = list_remove_head_type(&ep->queued_txns, iotxn_t, node);
            if (!txn) {
                // nothing to do
                return;
            }

            zx_status_t status = xhci_start_transfer_locked(xhci, slot, ep_index, txn);
            if (status == ZX_OK) {
                list_add_tail(&ep->pending_txns, &txn->node);
                ep->current_txn = txn;
            } else {
                txn->status = status;
                txn->actual = 0;
                list_add_tail(completed_txns, &txn->node);
            }
        }

        if (ep->current_txn) {
            iotxn_t* txn = ep->current_txn;
            zx_status_t status = xhci_continue_transfer_locked(xhci, slot, ep_index, txn);
            if (status == ZX_ERR_SHOULD_WAIT) {
                // no available TRBs - need to wait for some complete
                return;
            } else {
                if (status != ZX_OK) {
                    txn->status = status;
                    txn->actual = 0;
                    list_delete(&txn->node);
                    list_add_tail(completed_txns, &txn->node);
                }
                ep->current_txn = NULL;
            }
        }
    }
}

zx_status_t xhci_queue_transfer(xhci_t* xhci, iotxn_t* txn) {
    usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);
    uint32_t slot_id = proto_data->device_id;
    uint8_t ep_index = xhci_endpoint_index(proto_data->ep_address);
    __UNUSED usb_setup_t* setup = (ep_index == 0 ? &proto_data->setup : NULL);

    dprintf(LTRACE, "xhci_queue_transfer slot_id: %d setup: %p ep_index: %d length: %lu\n",
            slot_id, setup, ep_index, txn->length);

    int rh_index = xhci_get_root_hub_index(xhci, slot_id);
    if (rh_index >= 0) {
        return xhci_rh_iotxn_queue(xhci, txn, rh_index);
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
    case EP_STATE_PAUSED:
        status = ZX_OK;
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

    list_add_tail(&ep->queued_txns, &txn->node);

    list_node_t completed_txns = LIST_INITIAL_VALUE(completed_txns);
    xhci_process_transactions_locked(xhci, slot, ep_index, &completed_txns);

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((txn = list_remove_head_type(&completed_txns, iotxn_t, node)) != NULL) {
        iotxn_complete(txn, txn->status, txn->actual);
    }

    return ZX_OK;
}

zx_status_t xhci_cancel_transfers(xhci_t* xhci, uint32_t slot_id, uint32_t ep_index) {
    dprintf(TRACE, "xhci_cancel_transfers slot_id: %d ep_index: %d\n", slot_id, ep_index);

    if (slot_id < 1 || slot_id > xhci->max_slots) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (ep_index >= XHCI_NUM_EPS) {
        return ZX_ERR_INVALID_ARGS;
    }

    xhci_slot_t* slot = &xhci->slots[slot_id];
    xhci_endpoint_t* ep = &slot->eps[ep_index];
    list_node_t completed_txns = LIST_INITIAL_VALUE(completed_txns);
    iotxn_t* txn;
    iotxn_t* temp;
    zx_status_t status = ZX_OK;

    mtx_lock(&ep->lock);

    if (!list_is_empty(&ep->pending_txns)) {
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
            dprintf(ERROR, "xhci_cancel_transfers: TRB_CMD_STOP_ENDPOINT failed cc: %d\n", cc);
            return ZX_ERR_INTERNAL;
        }
        mtx_lock(&ep->lock);

        // TRB_CMD_STOP_ENDPOINT may have have completed a currently executing txn
        // but we may still have other pending txns. xhci_reset_dequeue_ptr_locked()
        // will set the dequeue pointer after the last completed txn.
        list_for_every_entry_safe(&ep->pending_txns, txn, temp, iotxn_t, node) {
            list_delete(&txn->node);
            txn->status = ZX_ERR_CANCELED;
            txn->actual = 0;
            list_add_head(&completed_txns, &txn->node);
        }

        status = xhci_reset_dequeue_ptr_locked(xhci, slot_id, ep_index);
        if (status == ZX_OK) {
            ep->state = EP_STATE_RUNNING;
        }
    }

    // elements of the queued_txns list can simply be removed and completed.
    list_for_every_entry_safe(&ep->queued_txns, txn, temp, iotxn_t, node) {
        list_delete(&txn->node);
        txn->status = ZX_ERR_CANCELED;
        txn->actual = 0;
        list_add_head(&completed_txns, &txn->node);
    }

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((txn = list_remove_head_type(&completed_txns, iotxn_t, node)) != NULL) {
        iotxn_complete(txn, txn->status, txn->actual);
    }

    return status;
}

static void xhci_control_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

int xhci_control_request(xhci_t* xhci, uint32_t slot_id, uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, void* data, uint16_t length) {

    dprintf(LTRACE, "xhci_control_request slot_id: %d type: 0x%02X req: %d value: %d index: %d "
            "length: %d\n", slot_id, request_type, request, value, index, length);

    iotxn_t* txn;

    // xhci_control_request is only used for reading first 8 bytes of the device descriptor,
    // so using IOTXN_ALLOC_POOL makes sense here.
    zx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_POOL, length);
    if (status != ZX_OK) return status;
    txn->protocol = ZX_PROTOCOL_USB;

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
    iotxn_queue(xhci->zxdev, txn);
    status = completion_wait(&completion, ZX_SEC(1));
    if (status == ZX_OK) {
        status = txn->status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        dprintf(ERROR, "xhci_control_request ZX_ERR_TIMED_OUT\n");
        completion_reset(&completion);
        status = xhci_cancel_transfers(xhci, slot_id, 0);
        if (status == ZX_OK) {
            completion_wait(&completion, ZX_TIME_INFINITE);
            status = ZX_ERR_TIMED_OUT;
        }
    }
    dprintf(TRACE, "xhci_cancel_transfer got %d\n", status);
    if (status == ZX_OK) {
        status = txn->actual;

        if (length > 0 && !out) {
            iotxn_copyfrom(txn, data, txn->actual, 0);
        }
    }
    iotxn_release(txn);
    dprintf(TRACE, "xhci_control_request returning %d\n", status);
    return status;
}

zx_status_t xhci_get_descriptor(xhci_t* xhci, uint32_t slot_id, uint8_t type, uint16_t value,
                                uint16_t index, void* data, uint16_t length) {
    return xhci_control_request(xhci, slot_id, USB_DIR_IN | type | USB_RECIP_DEVICE,
                                USB_REQ_GET_DESCRIPTOR, value, index, data, length);
}

void xhci_handle_transfer_event(xhci_t* xhci, xhci_trb_t* trb) {
    dprintf(LTRACE, "xhci_handle_transfer_event: %08X %08X %08X %08X\n",
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
    iotxn_t* txn = NULL;

    mtx_lock(&ep->lock);

    zx_status_t result;
    switch (cc) {
        case TRB_CC_SUCCESS:
        case TRB_CC_SHORT_PACKET:
            result = length;
            break;
        case TRB_CC_BABBLE_DETECTED_ERROR:
            dprintf(TRACE, "xhci_handle_transfer_event: TRB_CC_BABBLE_DETECTED_ERROR\n");
            result = ZX_ERR_IO_OVERRUN;
            break;
        case TRB_CC_USB_TRANSACTION_ERROR:
        case TRB_CC_TRB_ERROR:
        case TRB_CC_STALL_ERROR: {
            int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
            dprintf(TRACE, "xhci_handle_transfer_event: cc %d ep_ctx_state %d\n", cc, ep_ctx_state);
            if (ep_ctx_state == EP_CTX_STATE_HALTED || ep_ctx_state == EP_CTX_STATE_ERROR) {
                result = ZX_ERR_IO_REFUSED;
            } else {
                result = ZX_ERR_IO;
            }
            break;
        }
        case TRB_CC_RING_UNDERRUN:
            // non-fatal error that happens when no transfers are available for isochronous endpoint
            dprintf(TRACE, "xhci_handle_transfer_event: TRB_CC_RING_UNDERRUN\n");
            mtx_unlock(&ep->lock);
            return;
        case TRB_CC_RING_OVERRUN:
            // non-fatal error that happens when no transfers are available for isochronous endpoint
            dprintf(TRACE, "xhci_handle_transfer_event: TRB_CC_RING_OVERRUN\n");
            mtx_unlock(&ep->lock);
            return;
       case TRB_CC_MISSED_SERVICE_ERROR:
            dprintf(TRACE, "xhci_handle_transfer_event: TRB_CC_MISSED_SERVICE_ERROR\n");
            result = ZX_ERR_IO_MISSED_DEADLINE;
            break;
        case TRB_CC_STOPPED:
        case TRB_CC_STOPPED_LENGTH_INVALID:
        case TRB_CC_STOPPED_SHORT_PACKET:
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
                dprintf(ERROR, "xhci_handle_transfer_event: bad state for stopped txn: %d\n", ep->state);
                result = ZX_ERR_INTERNAL;
            }
            break;
        default: {
            int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
            dprintf(ERROR, "xhci_handle_transfer_event: unhandled transfer event condition code %d "
                   "ep_ctx_state %d:  %08X %08X %08X %08X\n", cc, ep_ctx_state,
                    ((uint32_t*)trb)[0], ((uint32_t*)trb)[1], ((uint32_t*)trb)[2], ((uint32_t*)trb)[3]);
            if (ep_ctx_state == EP_CTX_STATE_HALTED || ep_ctx_state == EP_CTX_STATE_ERROR) {
                result = ZX_ERR_IO_REFUSED;
            } else {
                result = ZX_ERR_IO;
            }
            break;
        }
    }

    if (trb_get_ptr(trb) && !list_is_empty(&ep->pending_txns)) {
        if (control & EVT_TRB_ED) {
            txn = (iotxn_t *)trb_get_ptr(trb);
        } else {
            trb = xhci_read_trb_ptr(ring, trb);
            for (uint i = 0; i < TRANSFER_RING_SIZE && trb; i++) {
                if (trb_get_type(trb) == TRB_TRANSFER_EVENT_DATA) {
                    txn = (iotxn_t *)trb_get_ptr(trb);
                    break;
                }
                trb = xhci_get_next_trb(ring, trb);
            }
        }
    }

    int ep_ctx_state = xhci_get_ep_ctx_state(slot, ep);
    if (ep_ctx_state != EP_CTX_STATE_RUNNING) {
        dprintf(TRACE, "xhci_handle_transfer_event: ep ep_ctx_state %d cc %d\n", ep_ctx_state, cc);
    }

    if (!txn) {
        // no txn expected for this condition code
        if (cc != TRB_CC_STOPPED_LENGTH_INVALID) {
            dprintf(ERROR, "xhci_handle_transfer_event: unable to find iotxn to complete!\n");
        }
        mtx_unlock(&ep->lock);
        return;
    }

    // when transaction errors occur, we sometimes receive multiple events for the same transfer.
    // here we check to make sure that this event doesn't correspond to a transfer that has already
    // been completed. In the typical case, the context will be found at the head of pending_txns.
    bool found_txn = false;
    iotxn_t* test;
    list_for_every_entry(&ep->pending_txns, test, iotxn_t, node) {
        if (test == txn) {
            found_txn = true;
            break;
        }
    }
    if (!found_txn) {
        dprintf(TRACE, "xhci_handle_transfer_event: ignoring transfer event for completed transfer\n");
        mtx_unlock(&ep->lock);
        return;
    }

    // update dequeue_ptr to TRB following this transaction
    ring->dequeue_ptr = txn->context;

    // remove txn from pending_txns
    list_delete(&txn->node);

    if (result < 0) {
        txn->status = result;
        txn->actual = 0;
    } else {
        txn->status = 0;
        txn->actual = result;
    }

    list_node_t completed_txns = LIST_INITIAL_VALUE(completed_txns);
    list_add_head(&completed_txns, &txn->node);

    if (result == ZX_ERR_IO_REFUSED && ep->state != EP_STATE_DEAD) {
        ep->state = EP_STATE_HALTED;
    } else if (ep->state == EP_STATE_RUNNING) {
        xhci_process_transactions_locked(xhci, slot, ep_index, &completed_txns);
    }

    mtx_unlock(&ep->lock);

    // call complete callbacks out of the lock
    while ((txn = list_remove_head_type(&completed_txns, iotxn_t, node)) != NULL) {
#if XHCI_USE_CACHE_OPS
        if (txn->actual > 0) {
            usb_protocol_data_t* proto_data = iotxn_pdata(txn, usb_protocol_data_t);
            uint8_t direction;

            if (proto_data->ep_address == 0) {
                direction = proto_data->setup.bmRequestType & USB_ENDPOINT_DIR_MASK;
            } else {
                direction = proto_data->ep_address & USB_ENDPOINT_DIR_MASK;
            }
            if (direction == USB_DIR_IN) {
                iotxn_cacheop(txn, ZX_VMO_OP_CACHE_INVALIDATE, 0, txn->actual);
            }
        }
#endif
        iotxn_complete(txn, txn->status, txn->actual);
    }
}
