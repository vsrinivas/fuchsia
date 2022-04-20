// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include "dwc3-regs.h"
#include "dwc3.h"

namespace dwc3 {

zx_status_t Dwc3::Fifo::Init(zx::bti& bti) {
  if (buffer.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  if (zx_status_t status = buffer.Init(bti.get(), Fifo::kFifoSize, IO_BUFFER_RW | IO_BUFFER_CONTIG);
      status != ZX_OK) {
    return status;
  }

  first = static_cast<dwc3_trb_t*>(buffer.virt());
  next = first;
  current = nullptr;
  last = first + (Fifo::kFifoSize / sizeof(dwc3_trb_t)) - 1;

  // set up link TRB pointing back to the start of the fifo
  dwc3_trb_t* trb = last;
  zx_paddr_t trb_phys = buffer.phys();
  trb->ptr_low = (uint32_t)trb_phys;
  trb->ptr_high = (uint32_t)(trb_phys >> 32);
  trb->status = 0;
  trb->control = TRB_TRBCTL_LINK | TRB_HWO;
  buffer.CacheFlush((trb - first) * sizeof(*trb), sizeof(*trb));

  return ZX_OK;
}

void Dwc3::Fifo::Release() {
  buffer.release();
  first = next = current = last = nullptr;
}

void Dwc3::EpEnable(const Endpoint& ep, bool enable) {
  fbl::AutoLock lock(&lock_);
  auto* mmio = get_mmio();

  if (enable) {
    DALEPENA::Get().ReadFrom(mmio).EnableEp(ep.ep_num).WriteTo(mmio);
  } else {
    DALEPENA::Get().ReadFrom(mmio).DisableEp(ep.ep_num).WriteTo(mmio);
  }
}

void Dwc3::EpSetConfig(Endpoint& ep, bool enable) {
  zxlogf(DEBUG, "Dwc3::EpSetConfig %u", ep.ep_num);

  if (enable) {
    CmdEpSetConfig(ep, false);
    CmdEpTransferConfig(ep);
    EpEnable(ep, true);
  } else {
    EpEnable(ep, false);
  }
}

zx_status_t Dwc3::EpSetStall(Endpoint& ep, bool stall) {
  if (!ep.enabled) {
    return ZX_ERR_BAD_STATE;
  }

  if (stall && !ep.stalled) {
    CmdEpSetStall(ep);
  } else if (!stall && ep.stalled) {
    CmdEpClearStall(ep);
  }

  ep.stalled = stall;

  return ZX_OK;
}

void Dwc3::EpStartTransfer(Endpoint& ep, Fifo& fifo, uint32_t type, zx_paddr_t buffer,
                           size_t length, bool send_zlp) {
  zxlogf(SERIAL, "Dwc3::EpStartTransfer ep %u type %u length %zu", ep.ep_num, type, length);

  dwc3_trb_t* trb = fifo.next++;
  if (fifo.next == fifo.last) {
    fifo.next = fifo.first;
  }
  if (fifo.current == nullptr) {
    fifo.current = trb;
  }

  trb->ptr_low = static_cast<uint32_t>(buffer);
  trb->ptr_high = static_cast<uint32_t>(buffer >> 32);
  trb->status = TRB_BUFSIZ(static_cast<uint32_t>(length));
  if (send_zlp) {
    trb->control = type | TRB_HWO;
  } else {
    trb->control = type | TRB_LST | TRB_IOC | TRB_HWO;
  }
  fifo.buffer.CacheFlush((trb - fifo.first) * sizeof(*trb), sizeof(*trb));

  if (send_zlp) {
    dwc3_trb_t* zlp_trb = fifo.next++;
    if (fifo.next == fifo.last) {
      fifo.next = fifo.first;
    }
    zlp_trb->ptr_low = 0;
    zlp_trb->ptr_high = 0;
    zlp_trb->status = TRB_BUFSIZ(0);
    zlp_trb->control = type | TRB_LST | TRB_IOC | TRB_HWO;
    fifo.buffer.CacheFlush((zlp_trb - fifo.first) * sizeof(*trb), sizeof(*trb));
  }

  CmdEpStartTransfer(ep, fifo.GetTrbPhys(trb));
}

void Dwc3::EpEndTransfers(Endpoint& ep, zx_status_t reason) {
  if (ep.current_req) {
    CmdEpEndTransfer(ep);

    usb_request_t* req = ep.current_req;
    ep.current_req = nullptr;
    req->response.status = reason;
    req->response.actual = 0;
    pending_completions_.push(Request{req, sizeof(*req)});
  }
  ep.got_not_ready = false;

  for (std::optional<Request> req = ep.queued_reqs.pop(); req; req = ep.queued_reqs.pop()) {
    req->request()->response.status = reason;
    req->request()->response.actual = 0;
    pending_completions_.push(std::move(*req));
  }
}

void Dwc3::EpReadTrb(Endpoint& ep, Fifo& fifo, const dwc3_trb_t* src, dwc3_trb_t* dst) {
  if (src >= fifo.first && src < fifo.last) {
    fifo.buffer.CacheFlushInvalidate((src - fifo.first) * sizeof(*src), sizeof(*src));
    memcpy(dst, src, sizeof(*dst));
  } else {
    zxlogf(ERROR, "bad trb");
  }
}

void Dwc3::UserEpQueueNext(UserEndpoint& uep) {
  Endpoint& ep = uep.ep;
  std::optional<Request> opt_req;

  if ((ep.current_req == nullptr) && ep.got_not_ready) {
    opt_req = ep.queued_reqs.pop();
  }

  if (opt_req.has_value()) {
    usb_request_t* req = ep.current_req = opt_req->take();
    ep.got_not_ready = false;

    if (ep.IsInput()) {
      usb_request_cache_flush(req, 0, req->header.length);
    } else {
      usb_request_cache_flush_invalidate(req, 0, req->header.length);
    }

    // TODO(voydanoff) scatter/gather support
    phys_iter_t iter;
    zx_paddr_t phys;
    usb_request_physmap(req, bti_.get());
    usb_request_phys_iter_init(&iter, req, zx_system_get_page_size());
    usb_request_phys_iter_next(&iter, &phys);
    bool send_zlp = req->header.send_zlp && ((req->header.length % ep.max_packet_size) == 0);
    EpStartTransfer(ep, uep.fifo, TRB_TRBCTL_NORMAL, phys, req->header.length, send_zlp);
  }
}

zx_status_t Dwc3::UserEpCancelAll(UserEndpoint& uep) {
  RequestQueue to_complete;
  {
    fbl::AutoLock lock(&uep.lock);
    to_complete = UserEpCancelAllLocked(uep);
  }

  // Now that we have dropped the lock, go ahead and complete all of the
  // requests we canceled.
  to_complete.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
  return ZX_OK;
}

Dwc3::RequestQueue Dwc3::UserEpCancelAllLocked(UserEndpoint& uep) {
  RequestQueue to_complete;

  // Move the endpoint's queue of requests into a local list so we can
  // complete the requests outside of the endpoint lock.
  to_complete = std::move(uep.ep.queued_reqs);

  // If there is currently a request in-flight, be sure to cancel its
  // transfer, and add the in-flight request to the local queue of requests to
  // complete.  Make sure we add this in-flight request to the _front_ of the
  // queue so that all requests are completed in the order that they were
  // queued.
  if (uep.ep.current_req != nullptr) {
    CmdEpEndTransfer(uep.ep);
    to_complete.push_next(Request{uep.ep.current_req, sizeof(*uep.ep.current_req)});
    uep.ep.current_req = nullptr;
  }

  // Return the list of requests back to the caller so they can complete them
  // once the enpoint's lock has finally been dropped.
  return to_complete;
}

void Dwc3::HandleEpTransferCompleteEvent(uint8_t ep_num) {
  if (is_ep0_num(ep_num)) {
    HandleEp0TransferCompleteEvent(ep_num);
    return;
  }

  usb_request_t* req = nullptr;
  {
    UserEndpoint* const uep = get_user_endpoint(ep_num);
    ZX_DEBUG_ASSERT(uep != nullptr);

    fbl::AutoLock lock{&uep->lock};
    std::swap(req, uep->ep.current_req);

    if (req) {
      dwc3_trb_t trb;
      EpReadTrb(uep->ep, uep->fifo, uep->fifo.current, &trb);
      uep->fifo.current = nullptr;

      if (trb.control & TRB_HWO) {
        zxlogf(ERROR, "TRB_HWO still set in dwc3_ep_xfer_complete");
      }

      req->response.actual = req->header.length - TRB_BUFSIZ(trb.status);
      req->response.status = ZX_OK;
    }
  }

  if (req) {
    pending_completions_.push(Request{req, sizeof(*req)});
  } else {
    zxlogf(ERROR, "no usb request found to complete!");
  }
}

void Dwc3::HandleEpTransferNotReadyEvent(uint8_t ep_num, uint32_t stage) {
  if (is_ep0_num(ep_num)) {
    HandleEp0TransferNotReadyEvent(ep_num, stage);
    return;
  }

  UserEndpoint* const uep = get_user_endpoint(ep_num);
  ZX_DEBUG_ASSERT(uep != nullptr);

  fbl::AutoLock lock(&uep->lock);
  uep->ep.got_not_ready = true;
  UserEpQueueNext(*uep);
}

void Dwc3::HandleEpTransferStartedEvent(uint8_t ep_num, uint32_t rsrc_id) {
  if (is_ep0_num(ep_num)) {
    fbl::AutoLock ep0_lock(&ep0_.lock);
    ((ep_num == kEp0Out) ? ep0_.out : ep0_.in).rsrc_id = rsrc_id;
  } else {
    UserEndpoint* const uep = get_user_endpoint(ep_num);
    ZX_DEBUG_ASSERT(uep != nullptr);

    fbl::AutoLock lock(&uep->lock);
    uep->ep.rsrc_id = rsrc_id;
  }
}

}  // namespace dwc3
