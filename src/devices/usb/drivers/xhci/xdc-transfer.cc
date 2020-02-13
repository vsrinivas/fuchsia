// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xdc-transfer.h"

#include <ddk/debug.h>

#include "xdc.h"

namespace usb_xhci {

// Reads a range of bits from an integer.
#define READ_FIELD(i, start, bits) (((i) >> (start)) & ((1 << (bits)) - 1))

static void xdc_ring_doorbell(xdc_t* xdc, xdc_endpoint_t* ep) {
  uint8_t doorbell_val = ep->direction == USB_DIR_IN ? DCDB_DB_EP_IN : DCDB_DB_EP_OUT;
  XHCI_SET_BITS32(&xdc->debug_cap_regs->dcdb, DCDB_DB_START, DCDB_DB_BITS, doorbell_val);
}

// Stores the value of the Dequeue Pointer into out_dequeue.
// Returns ZX_OK if successful, or ZX_ERR_BAD_STATE if the endpoint was not in the Stopped state.
static zx_status_t xdc_get_dequeue_ptr_locked(xdc_t* xdc, xdc_endpoint_t* ep, uint64_t* out_dequeue)
    __TA_REQUIRES(xdc->lock) {
  if (ep->state != XDC_EP_STATE_STOPPED) {
    zxlogf(ERROR, "tried to read dequeue pointer of %s EP while not stopped, state is: %d\n",
           ep->name, ep->state);
    return ZX_ERR_BAD_STATE;
  }
  xdc_context_data_t* ctx = xdc->context_data;
  xhci_endpoint_context_t* epc = ep->direction == USB_DIR_OUT ? &ctx->out_epc : &ctx->in_epc;

  uint64_t dequeue_ptr_hi = XHCI_READ32(&epc->tr_dequeue_hi);
  uint32_t dequeue_ptr_lo = XHCI_READ32(&epc->epc2) & EP_CTX_TR_DEQUEUE_LO_MASK;
  *out_dequeue = (dequeue_ptr_hi << 32 | dequeue_ptr_lo);
  return ZX_OK;
}

// Returns ZX_OK if the request was scheduled successfully, or ZX_ERR_SHOULD_WAIT
// if we ran out of TRBs.
static zx_status_t xdc_schedule_transfer_locked(xdc_t* xdc, xdc_endpoint_t* ep, usb_request_t* req)
    __TA_REQUIRES(xdc->lock) {
  xhci_transfer_ring_t* ring = &ep->transfer_ring;

  // Need to clean the cache for both IN and OUT transfers, invalidate only for IN.
  if (ep->direction == USB_DIR_IN) {
    usb_request_cache_flush_invalidate(req, 0, req->header.length);
  } else {
    usb_request_cache_flush(req, 0, req->header.length);
  }

  zx_status_t status = xhci_queue_data_trbs(ring, &ep->transfer_state, req, 0 /* interrupter */,
                                            false /* isochronous */);
  if (status != ZX_OK) {
    return status;
  }

  // If we get here, then we are ready to ring the doorbell.
  // Save the ring position so we can update the ring dequeue ptr once the transfer completes.
  xdc_req_internal_t* req_int = USB_REQ_TO_XDC_INTERNAL(req, sizeof(usb_request_t));
  req_int->context = ring->current_trb;
  xdc_ring_doorbell(xdc, ep);

  return ZX_OK;
}

// Schedules any queued requests on the endpoint's transfer ring, until we fill our
// transfer ring or have no more requests.
void xdc_process_transactions_locked(xdc_t* xdc, xdc_endpoint_t* ep) __TA_REQUIRES(xdc->lock) {
  uint64_t usb_req_size = sizeof(usb_request_t);
  zx_status_t status;
  while (1) {
    if (xhci_transfer_ring_free_trbs(&ep->transfer_ring) == 0) {
      // No available TRBs - need to wait for some to complete.
      return;
    }

    if (!ep->current_req) {
      // Start the next transaction in the queue.
      usb_request_t* req = xdc_req_list_remove_head(&ep->queued_reqs, usb_req_size);
      if (!req) {
        // No requests waiting.
        return;
      }
      xhci_transfer_state_init(&ep->transfer_state, req, USB_ENDPOINT_BULK, EP_CTX_MAX_PACKET_SIZE);
      status = xdc_req_list_add_tail(&ep->pending_reqs, req, usb_req_size);
      ZX_DEBUG_ASSERT(status == ZX_OK);
      ep->current_req = req;
    }

    usb_request_t* req = ep->current_req;
    status = xdc_schedule_transfer_locked(xdc, ep, req);
    if (status == ZX_ERR_SHOULD_WAIT) {
      // No available TRBs - need to wait for some to complete.
      return;
    } else {
      ep->current_req = nullptr;
    }
  }
}

zx_status_t xdc_queue_transfer(xdc_t* xdc, usb_request_t* req, bool in, bool is_ctrl_msg) {
  xdc_endpoint_t* ep = in ? &xdc->eps[IN_EP_IDX] : &xdc->eps[OUT_EP_IDX];

  mtx_lock(&xdc->lock);

  // We should always queue control messages unless there is an unrecoverable error.
  if (!is_ctrl_msg && (!xdc->configured || ep->state == XDC_EP_STATE_DEAD)) {
    mtx_unlock(&xdc->lock);
    return ZX_ERR_IO_NOT_PRESENT;
  }

  if (req->header.length > 0) {
    zx_status_t status = usb_request_physmap(req, xdc->bti_handle);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: usb_request_physmap failed: %d\n", __FUNCTION__, status);
      mtx_unlock(&xdc->lock);
      return status;
    }
  }

  xdc_req_internal_t* req_int = USB_REQ_TO_XDC_INTERNAL(req, sizeof(usb_request_t));
  req_int->complete_cb.callback = in ? xdc_read_complete : xdc_write_complete;
  req_int->complete_cb.ctx = xdc;

  list_add_tail(&ep->queued_reqs, &req_int->node);

  // We can still queue requests for later while waiting for the xdc device to be configured,
  // or while the endpoint is halted. Before scheduling the TRBs however, we should wait
  // for the device to be configured, and/or the halt is cleared by DbC and we've cleaned
  // up the transfer ring.
  if (xdc->configured && ep->state == XDC_EP_STATE_RUNNING) {
    xdc_process_transactions_locked(xdc, ep);
  }

  mtx_unlock(&xdc->lock);

  return ZX_OK;
}

bool xdc_has_free_trbs(xdc_t* xdc, bool in) {
  mtx_lock(&xdc->lock);

  xdc_endpoint_t* ep = in ? &xdc->eps[IN_EP_IDX] : &xdc->eps[OUT_EP_IDX];
  bool has_trbs = xhci_transfer_ring_free_trbs(&ep->transfer_ring) > 0;

  mtx_unlock(&xdc->lock);
  return has_trbs;
}

zx_status_t xdc_restart_transfer_ring_locked(xdc_t* xdc, xdc_endpoint_t* ep) {
  // Once the DbC clears the halt flag for the endpoint, the address stored in the
  // TR Dequeue Pointer field is the next TRB to be executed (see XHCI Spec 7.6.4.3).
  // There seems to be no guarantee which TRB this will point to.
  //
  // The easiest way to deal with this is to convert all scheduled TRBs to NO-OPs,
  // and reschedule pending requests.

  uint64_t dequeue_ptr;
  zx_status_t status = xdc_get_dequeue_ptr_locked(xdc, ep, &dequeue_ptr);
  if (status != ZX_OK) {
    return status;
  }
  xhci_transfer_ring_t* ring = &ep->transfer_ring;
  xhci_trb_t* trb = xhci_transfer_ring_phys_to_trb(ring, dequeue_ptr);
  if (!trb) {
    zxlogf(ERROR, "no valid TRB corresponding to dequeue_ptr: %lu\n", dequeue_ptr);
    return ZX_ERR_BAD_STATE;
  }

  // Reset our copy of the dequeue pointer.
  xhci_set_dequeue_ptr(ring, trb);

  // Convert all pending TRBs on the transfer ring into NO-OPs TRBs.
  // ring->current is just after our last queued TRB.
  xhci_trb_t* last_trb = nullptr;
  while (trb != ring->current_trb) {
    xhci_set_transfer_noop_trb(trb);
    last_trb = trb;
    trb = xhci_get_next_trb(ring, trb);
  }
  if (last_trb) {
    // Set IOC (Interrupt on Completion) on the last NO-OP TRB, so we know
    // when we can overwrite them in the transfer ring.
    uint32_t control = XHCI_READ32(&last_trb->control);
    XHCI_WRITE32(&last_trb->control, control | XFER_TRB_IOC);
  }
  // Restart the transfer ring.
  xdc_ring_doorbell(xdc, ep);
  ep->state = XDC_EP_STATE_RUNNING;

  // Requeue and reschedule the requests.
  usb_request_t* req;
  uint64_t usb_req_size = sizeof(usb_request_t);
  while ((req = xdc_req_list_remove_tail(&ep->pending_reqs, usb_req_size)) != nullptr) {
    status = xdc_req_list_add_head(&ep->queued_reqs, req, usb_req_size);
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }
  xdc_process_transactions_locked(xdc, ep);
  return ZX_OK;
}

void xdc_handle_transfer_event_locked(xdc_t* xdc, xdc_poll_state_t* poll_state, xhci_trb_t* trb) {
  uint32_t control = XHCI_READ32(&trb->control);
  uint32_t status = XHCI_READ32(&trb->status);
  uint32_t ep_dev_ctx_idx = READ_FIELD(control, TRB_ENDPOINT_ID_START, TRB_ENDPOINT_ID_BITS);
  uint8_t xdc_ep_idx = ep_dev_ctx_idx == EP_IN_DEV_CTX_IDX ? IN_EP_IDX : OUT_EP_IDX;
  xdc_endpoint_t* ep = &xdc->eps[xdc_ep_idx];
  xhci_transfer_ring_t* ring = &ep->transfer_ring;
  uint64_t usb_req_size = sizeof(usb_request_t);

  uint32_t cc = READ_FIELD(status, EVT_TRB_CC_START, EVT_TRB_CC_BITS);
  uint32_t length = READ_FIELD(status, EVT_TRB_XFER_LENGTH_START, EVT_TRB_XFER_LENGTH_BITS);
  usb_request_t* req = nullptr;
  bool error = false;

  switch (cc) {
    case TRB_CC_SUCCESS:
    case TRB_CC_SHORT_PACKET:
      break;
    case TRB_CC_BABBLE_DETECTED_ERROR:
    case TRB_CC_USB_TRANSACTION_ERROR:
    case TRB_CC_TRB_ERROR:
    case TRB_CC_STALL_ERROR:
      zxlogf(ERROR, "xdc_handle_transfer_event: error condition code: %d\n", cc);
      error = true;
      break;
    default:
      zxlogf(ERROR, "xdc_handle_transfer_event: unexpected condition code %d\n", cc);
      error = true;
      break;
  }

  // Even though the main poll loop checks for changes in the halt registers,
  // it's possible we missed the halt register being set if the halt was cleared fast enough.
  if (error) {
    if (ep->state == XDC_EP_STATE_RUNNING) {
      xdc_endpoint_set_halt_locked(xdc, poll_state, ep);
    }
    ep->got_err_event = true;
    // We're going to requeue the transfer when we restart the transfer ring,
    // so nothing else to do.
    return;
  }

  if (control & EVT_TRB_ED) {
    // An Event Data TRB generated the completion event, so the TRB Pointer field
    // will contain the usb request pointer we previously stored.
    req = reinterpret_cast<usb_request_t*>(trb->ptr);
  } else {
    // Get the pointer to the TRB that generated the event.
    trb = xhci_read_trb_ptr(ring, trb);
    if (trb_get_type(trb) == TRB_TRANSFER_NOOP) {
      // If it's the NO-OP TRB we queued when dealing with the halt condition,
      // there won't be a corresponding usb request.
      zxlogf(TRACE, "xdc_handle_transfer_event: got a NO-OP TRB\n");
      xhci_set_dequeue_ptr(ring, xhci_get_next_trb(ring, trb));
      xdc_process_transactions_locked(xdc, ep);
      return;
    }

    // Look for the Event Data TRB which will have the usb request pointer.
    for (uint i = 0; i < TRANSFER_RING_SIZE && trb; i++) {
      if (trb_get_type(trb) == TRB_TRANSFER_EVENT_DATA) {
        req = reinterpret_cast<usb_request_t*>(trb->ptr);
        break;
      }
      trb = xhci_get_next_trb(ring, trb);
    }
  }

  if (!req) {
    zxlogf(ERROR, "xdc_handle_transfer_event: unable to find request to complete\n");
    return;
  }

  // Find the usb request in the pending list.
  bool found_req = false;
  usb_request_t* test;
  xdc_req_internal_t* test_int;
  list_for_every_entry (&ep->pending_reqs, test_int, xdc_req_internal_t, node) {
    test = XDC_INTERNAL_TO_USB_REQ(test_int, usb_req_size);
    if (test == req) {
      found_req = true;
      break;
    }
  }
  if (!found_req) {
    zxlogf(ERROR, "xdc_handle_transfer_event: ignoring event for completed transfer\n");
    return;
  }
  // Remove request from pending_reqs.
  list_delete(&test_int->node);

  // Update our copy of the dequeue_ptr to the TRB following this transaction.
  xhci_set_dequeue_ptr(ring, static_cast<xhci_trb_t*>(test_int->context));
  xdc_process_transactions_locked(xdc, ep);

  // Save the request to be completed later out of the lock.
  req->response.status = ZX_OK;
  req->response.actual = length;
  status = xdc_req_list_add_tail(&poll_state->completed_reqs, req, usb_req_size);
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

}  // namespace usb_xhci
