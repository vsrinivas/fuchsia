// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>

#include "dwc3-regs.h"
#include "dwc3-types.h"
#include "dwc3.h"

#define EP_FIFO_SIZE PAGE_SIZE

static zx_paddr_t dwc3_ep_trb_phys(dwc3_endpoint_t* ep, dwc3_trb_t* trb) {
  return io_buffer_phys(&ep->fifo.buffer) + (trb - ep->fifo.first) * sizeof(*trb);
}

static void dwc3_enable_ep(dwc3_t* dwc, unsigned ep_num, bool enable) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  if (enable) {
    DALEPENA::Get().ReadFrom(mmio).EnableEp(ep_num).WriteTo(mmio);
  } else {
    DALEPENA::Get().ReadFrom(mmio).DisableEp(ep_num).WriteTo(mmio);
  }
}

zx_status_t dwc3_ep_fifo_init(dwc3_t* dwc, unsigned ep_num) {
  ZX_DEBUG_ASSERT(ep_num < countof(dwc->eps));
  dwc3_endpoint_t* ep = &dwc->eps[ep_num];
  dwc3_fifo_t* fifo = &ep->fifo;

  static_assert(EP_FIFO_SIZE <= PAGE_SIZE, "");
  zx_status_t status = io_buffer_init(&fifo->buffer, dwc->bti_handle.get(), EP_FIFO_SIZE,
                                      IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    return status;
  }

  fifo->first = static_cast<dwc3_trb_t*>(io_buffer_virt(&fifo->buffer));
  fifo->next = fifo->first;
  fifo->current = nullptr;
  fifo->last = fifo->first + (EP_FIFO_SIZE / sizeof(dwc3_trb_t)) - 1;

  // set up link TRB pointing back to the start of the fifo
  dwc3_trb_t* trb = fifo->last;
  zx_paddr_t trb_phys = io_buffer_phys(&fifo->buffer);
  trb->ptr_low = (uint32_t)trb_phys;
  trb->ptr_high = (uint32_t)(trb_phys >> 32);
  trb->status = 0;
  trb->control = TRB_TRBCTL_LINK | TRB_HWO;
  io_buffer_cache_flush(&ep->fifo.buffer, (trb - ep->fifo.first) * sizeof(*trb), sizeof(*trb));

  return ZX_OK;
}

void dwc3_ep_fifo_release(dwc3_t* dwc, unsigned ep_num) {
  ZX_DEBUG_ASSERT(ep_num < countof(dwc->eps));
  dwc3_endpoint_t* ep = &dwc->eps[ep_num];

  io_buffer_release(&ep->fifo.buffer);
}

void dwc3_ep_start_transfer(dwc3_t* dwc, unsigned ep_num, unsigned type, zx_paddr_t buffer,
                            size_t length, bool send_zlp) {
  zxlogf(SERIAL, "dwc3_ep_start_transfer ep %u type %u length %zu", ep_num, type, length);

  // special case: EP0_OUT and EP0_IN use the same fifo
  dwc3_endpoint_t* ep = (ep_num == EP0_IN ? &dwc->eps[EP0_OUT] : &dwc->eps[ep_num]);

  dwc3_trb_t* trb = ep->fifo.next++;
  if (ep->fifo.next == ep->fifo.last) {
    ep->fifo.next = ep->fifo.first;
  }
  if (ep->fifo.current == nullptr) {
    ep->fifo.current = trb;
  }

  trb->ptr_low = (uint32_t)buffer;
  trb->ptr_high = (uint32_t)(buffer >> 32);
  trb->status = TRB_BUFSIZ(static_cast<uint32_t>(length));
  if (send_zlp) {
    trb->control = type | TRB_HWO;
  } else {
    trb->control = type | TRB_LST | TRB_IOC | TRB_HWO;
  }
  io_buffer_cache_flush(&ep->fifo.buffer, (trb - ep->fifo.first) * sizeof(*trb), sizeof(*trb));

  if (send_zlp) {
    dwc3_trb_t* zlp_trb = ep->fifo.next++;
    if (ep->fifo.next == ep->fifo.last) {
      ep->fifo.next = ep->fifo.first;
    }
    zlp_trb->ptr_low = 0;
    zlp_trb->ptr_high = 0;
    zlp_trb->status = TRB_BUFSIZ(0);
    zlp_trb->control = type | TRB_LST | TRB_IOC | TRB_HWO;
    io_buffer_cache_flush(&ep->fifo.buffer, (zlp_trb - ep->fifo.first) * sizeof(*trb),
                          sizeof(*trb));
  }

  dwc3_cmd_ep_start_transfer(dwc, ep_num, dwc3_ep_trb_phys(ep, trb));
}

static void dwc3_ep_queue_next_locked(dwc3_t* dwc, dwc3_endpoint_t* ep) {
  dwc_usb_req_internal_t* req_int;

  if (ep->current_req == nullptr && ep->got_not_ready &&
      (req_int = list_remove_head_type(&ep->queued_reqs, dwc_usb_req_internal_t, node)) !=
          nullptr) {
    usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
    ep->current_req = req;
    ep->got_not_ready = false;
    if (EP_IN(ep->ep_num)) {
      usb_request_cache_flush(req, 0, req->header.length);
    } else {
      usb_request_cache_flush_invalidate(req, 0, req->header.length);
    }

    // TODO(voydanoff) scatter/gather support
    phys_iter_t iter;
    zx_paddr_t phys;
    usb_request_physmap(req, dwc->bti_handle.get());
    usb_request_phys_iter_init(&iter, req, PAGE_SIZE);
    usb_request_phys_iter_next(&iter, &phys);
    bool send_zlp = req->header.send_zlp && (req->header.length % ep->max_packet_size) == 0;
    dwc3_ep_start_transfer(dwc, ep->ep_num, TRB_TRBCTL_NORMAL, phys, req->header.length, send_zlp);
  }
}

zx_status_t dwc3_ep_config(dwc3_t* dwc, const usb_endpoint_descriptor_t* ep_desc,
                           const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
  // convert address to index in range 0 - 31
  // low bit is IN/OUT
  unsigned ep_num = dwc3_ep_num(ep_desc->bEndpointAddress);
  if (ep_num < 2) {
    // index 0 and 1 are for endpoint zero
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t ep_type = usb_ep_type(ep_desc);
  if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
    zxlogf(ERROR, "dwc3_ep_config: isochronous endpoints are not supported");
    return ZX_ERR_NOT_SUPPORTED;
  }

  dwc3_endpoint_t* ep = &dwc->eps[ep_num];

  fbl::AutoLock lock(&ep->lock);

  zx_status_t status = dwc3_ep_fifo_init(dwc, ep_num);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwc3_config_ep: dwc3_ep_fifo_init failed %d", status);
    return status;
  }
  ep->max_packet_size = usb_ep_max_packet(ep_desc);
  ep->type = ep_type;
  ep->interval = ep_desc->bInterval;
  // TODO(voydanoff) USB3 support

  ep->enabled = true;

  if (dwc->configured) {
    dwc3_ep_queue_next_locked(dwc, ep);
  }

  return ZX_OK;
}

zx_status_t dwc3_ep_disable(dwc3_t* dwc, uint8_t ep_addr) {
  // convert address to index in range 0 - 31
  // low bit is IN/OUT
  unsigned ep_num = dwc3_ep_num(ep_addr);
  if (ep_num < 2) {
    // index 0 and 1 are for endpoint zero
    return ZX_ERR_INVALID_ARGS;
  }

  dwc3_endpoint_t* ep = &dwc->eps[ep_num];

  fbl::AutoLock lock(&ep->lock);

  dwc3_ep_fifo_release(dwc, ep_num);
  ep->enabled = false;

  return ZX_OK;
}

void dwc3_ep_queue(dwc3_t* dwc, unsigned ep_num, usb_request_t* req) {
  dwc3_endpoint_t* ep = &dwc->eps[ep_num];
  auto* req_int = USB_REQ_TO_INTERNAL(req);

  // OUT transactions must have length > 0 and multiple of max packet size
  if (EP_OUT(ep_num)) {
    if (req->header.length == 0 || req->header.length % ep->max_packet_size != 0) {
      zxlogf(ERROR, "dwc3_ep_queue: OUT transfers must be multiple of max packet size");
      usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, &req_int->complete_cb);
      return;
    }
  }

  fbl::AutoLock lock(&ep->lock);

  if (!ep->enabled) {
    usb_request_complete(req, ZX_ERR_BAD_STATE, 0, &req_int->complete_cb);
    return;
  }

  list_add_tail(&ep->queued_reqs, &req_int->node);

  if (dwc->configured) {
    dwc3_ep_queue_next_locked(dwc, ep);
  }
}

void dwc3_ep_set_config(dwc3_t* dwc, unsigned ep_num, bool enable) {
  zxlogf(DEBUG, "dwc3_ep_set_config %u", ep_num);

  dwc3_endpoint_t* ep = &dwc->eps[ep_num];

  if (enable) {
    dwc3_cmd_ep_set_config(dwc, ep_num, ep->type, ep->max_packet_size, ep->interval, false);
    dwc3_cmd_ep_transfer_config(dwc, ep_num);
    dwc3_enable_ep(dwc, ep_num, true);
  } else {
    dwc3_enable_ep(dwc, ep_num, false);
  }
}

void dwc3_start_eps(dwc3_t* dwc) {
  zxlogf(DEBUG, "dwc3_start_eps");

  dwc3_cmd_ep_set_config(dwc, EP0_IN, USB_ENDPOINT_CONTROL, dwc->eps[EP0_IN].max_packet_size, 0,
                         true);
  dwc3_cmd_start_new_config(dwc, EP0_OUT, 2);

  for (unsigned ep_num = 2; ep_num < countof(dwc->eps); ep_num++) {
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];
    if (ep->enabled) {
      dwc3_ep_set_config(dwc, ep_num, true);

      fbl::AutoLock lock(&ep->lock);
      dwc3_ep_queue_next_locked(dwc, ep);
    }
  }
}

void dwc_ep_read_trb(dwc3_endpoint_t* ep, dwc3_trb_t* trb, dwc3_trb_t* out_trb) {
  if (trb >= ep->fifo.first && trb < ep->fifo.last) {
    io_buffer_cache_flush_invalidate(&ep->fifo.buffer, (trb - ep->fifo.first) * sizeof(*trb),
                                     sizeof(*trb));
    memcpy((void*)out_trb, (void*)trb, sizeof(*trb));
  } else {
    zxlogf(ERROR, "dwc_ep_read_trb: bad trb");
  }
}

void dwc3_ep_xfer_started(dwc3_t* dwc, unsigned ep_num, unsigned rsrc_id) {
  dwc3_endpoint_t* ep = &dwc->eps[ep_num];
  fbl::AutoLock lock(&ep->lock);

  ep->rsrc_id = rsrc_id;
}

void dwc3_ep_xfer_not_ready(dwc3_t* dwc, unsigned ep_num, unsigned stage) {
  zxlogf(SERIAL, "dwc3_ep_xfer_not_ready ep %u state %d", ep_num, dwc->ep0_state);

  if (ep_num == EP0_OUT || ep_num == EP0_IN) {
    dwc3_ep0_xfer_not_ready(dwc, ep_num, stage);
  } else {
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];

    fbl::AutoLock lock(&ep->lock);
    ep->got_not_ready = true;
    dwc3_ep_queue_next_locked(dwc, ep);
  }
}

void dwc3_ep_xfer_complete(dwc3_t* dwc, unsigned ep_num) {
  zxlogf(SERIAL, "dwc3_ep_xfer_complete ep %u state %d", ep_num, dwc->ep0_state);

  if (ep_num >= countof(dwc->eps)) {
    zxlogf(ERROR, "dwc3_ep_xfer_complete: bad ep_num %u", ep_num);
    return;
  }

  if (ep_num == EP0_OUT || ep_num == EP0_IN) {
    dwc3_ep0_xfer_complete(dwc, ep_num);
  } else {
    dwc3_endpoint_t* ep = &dwc->eps[ep_num];

    ep->lock.Acquire();
    usb_request_t* req = ep->current_req;
    ep->current_req = nullptr;

    if (req) {
      dwc3_trb_t trb;
      dwc_ep_read_trb(ep, ep->fifo.current, &trb);
      ep->fifo.current = nullptr;
      if (trb.control & TRB_HWO) {
        zxlogf(ERROR, "TRB_HWO still set in dwc3_ep_xfer_complete");
      }

      zx_off_t actual = req->header.length - TRB_BUFSIZ(trb.status);
      //            dwc3_ep_queue_next_locked(dwc, ep);

      ep->lock.Release();

      auto* req_int = USB_REQ_TO_INTERNAL(req);
      fbl::AutoLock l(&dwc->pending_completions_lock);
      req->response.actual = actual;
      req->response.status = ZX_OK;
      list_add_tail(&dwc->pending_completions, &req_int->pending_node);
    } else {
      ep->lock.Release();
      zxlogf(ERROR, "dwc3_ep_xfer_complete: no usb request found to complete!");
    }
  }
}

zx_status_t dwc3_ep_set_stall(dwc3_t* dwc, unsigned ep_num, bool stall) {
  if (ep_num >= countof(dwc->eps)) {
    return ZX_ERR_INVALID_ARGS;
  }

  dwc3_endpoint_t* ep = &dwc->eps[ep_num];
  fbl::AutoLock lock(&ep->lock);

  if (!ep->enabled) {
    return ZX_ERR_BAD_STATE;
  }
  if (stall && !ep->stalled) {
    dwc3_cmd_ep_set_stall(dwc, ep_num);
  } else if (!stall && ep->stalled) {
    dwc3_cmd_ep_clear_stall(dwc, ep_num);
  }
  ep->stalled = stall;

  return ZX_OK;
}

void dwc3_ep_end_transfers(dwc3_t* dwc, unsigned ep_num, zx_status_t reason) {
  dwc3_endpoint_t* ep = &dwc->eps[ep_num];
  fbl::AutoLock lock(&ep->lock);

  if (ep->current_req) {
    dwc3_cmd_ep_end_transfer(dwc, ep_num);
    auto* req_int = USB_REQ_TO_INTERNAL(ep->current_req);
    fbl::AutoLock l(&dwc->pending_completions_lock);
    ep->current_req->response.status = reason;
    ep->current_req->response.actual = 0;
    list_add_tail(&dwc->pending_completions, &req_int->pending_node);
    ep->current_req = nullptr;
  }

  dwc_usb_req_internal_t* req_int;
  while ((req_int = list_remove_head_type(&ep->queued_reqs, dwc_usb_req_internal_t, node)) !=
         nullptr) {
    usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
    fbl::AutoLock l(&dwc->pending_completions_lock);
    req->response.status = reason;
    req->response.actual = 0;
    list_add_tail(&dwc->pending_completions, &req_int->pending_node);
  }
}
