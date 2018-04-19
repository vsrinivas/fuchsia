// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xdc-transfer.h"

// Returns ZX_OK if the request was scheduled successfully, or ZX_ERR_SHOULD_WAIT
// if we ran out of TRBs.
static zx_status_t xdc_schedule_transfer_locked(xdc_t* xdc, xdc_endpoint_t* ep,
                                                usb_request_t* req) __TA_REQUIRES(ep->lock) {
    xhci_transfer_ring_t* ring = &ep->transfer_ring;

    // Need to clean the cache for both IN and OUT transfers, invalidate only for IN.
    if (ep->direction == USB_DIR_IN) {
        usb_request_cache_flush_invalidate(req, 0, req->header.length);
    } else {
        usb_request_cache_flush(req, 0, req->header.length);
    }

    zx_status_t status = xhci_queue_data_trbs(ring, &ep->transfer_state, req,
                                              0 /* interrupter */, false /* isochronous */);
    if (status != ZX_OK) {
        return status;
    }

    // If we get here, then we are ready to ring the doorbell.
    // Save the ring position so we can update the ring dequeue ptr once the transfer completes.
    req->context = (void *)ring->current;

    uint8_t doorbell_val = ep->direction == USB_DIR_IN ? DCDB_DB_EP_IN : DCDB_DB_EP_OUT;
    XHCI_SET_BITS32(&xdc->debug_cap_regs->dcdb, DCDB_DB_START, DCDB_DB_BITS, doorbell_val);

    return ZX_OK;
}

// Schedules any queued requests on the endpoint's transfer ring, until we fill our
// transfer ring or have no more requests.
// Any invalid requests will not be queued, and will be added to the invalid_reqs list.
static void xdc_process_transactions_locked(xdc_t* xdc, xdc_endpoint_t* ep,
                                             list_node_t* invalid_reqs) __TA_REQUIRES(ep->lock) {
    while (1) {
        if (xhci_transfer_ring_free_trbs(&ep->transfer_ring) == 0) {
            // No available TRBs - need to wait for some to complete.
            return;
        }

        while (!ep->current_req) {
            // Start the next transaction in the queue.
            usb_request_t* req = list_remove_head_type(&ep->queued_reqs, usb_request_t, node);
            if (!req) {
                // No requests waiting.
                return;
            }
            zx_status_t status = xhci_transfer_state_init(&ep->transfer_state,
                                                          req,
                                                          USB_ENDPOINT_BULK,
                                                          EP_CTX_MAX_PACKET_SIZE);
            if (status == ZX_OK) {
                list_add_tail(&ep->pending_reqs, &req->node);
                ep->current_req = req;
            } else {
                req->response.status = status;
                req->response.actual = 0;
                list_add_tail(invalid_reqs, &req->node);
            }
        }

        usb_request_t* req = ep->current_req;
        zx_status_t status = xdc_schedule_transfer_locked(xdc, ep, req);
        if (status == ZX_ERR_SHOULD_WAIT) {
            // No available TRBs - need to wait for some to complete.
            return;
        } else {
            ep->current_req = NULL;
        }
    }
}

zx_status_t xdc_queue_transfer(xdc_t* xdc, usb_request_t* req, bool in) {
    xdc_endpoint_t* ep = in ? &xdc->eps[IN_EP_IDX] : &xdc->eps[OUT_EP_IDX];

    mtx_lock(&ep->lock);

    mtx_lock(&xdc->configured_mutex);
    if (!xdc->configured) {
        mtx_unlock(&xdc->configured_mutex);
        mtx_unlock(&ep->lock);
        return ZX_ERR_IO_NOT_PRESENT;
    }
    mtx_unlock(&xdc->configured_mutex);

    // TODO(jocelyndang): handle when the endpoint is halted.

    list_add_tail(&ep->queued_reqs, &req->node);

    list_node_t invalid_reqs = LIST_INITIAL_VALUE(invalid_reqs);
    xdc_process_transactions_locked(xdc, ep, &invalid_reqs);

    mtx_unlock(&ep->lock);

    // Call complete callbacks for any invalid requests out of the lock.
    while ((req = list_remove_head_type(&invalid_reqs, usb_request_t, node)) != NULL) {
        usb_request_complete(req, req->response.status, req->response.actual);
    }
    return ZX_OK;
}