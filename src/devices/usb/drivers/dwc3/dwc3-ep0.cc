// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include "dwc3.h"

namespace dwc3 {

zx_status_t Dwc3::Ep0Init() {
  fbl::AutoLock lock(&ep0_.lock);

  if (zx_status_t status = ep0_.shared_fifo.Init(bti_); status != ZX_OK) {
    return status;
  }

  const std::array eps{&ep0_.out, &ep0_.in};
  for (Endpoint* ep : eps) {
    ep->enabled = false;
    ep->max_packet_size = kEp0MaxPacketSize;
    ep->type = USB_ENDPOINT_CONTROL;
    ep->interval = 0;
  }

  return ZX_OK;
}

void Dwc3::Ep0Reset() {
  fbl::AutoLock lock(&ep0_.lock);
  CmdEpEndTransfer(ep0_.out);
  ep0_.state = Ep0::State::None;
}

void Dwc3::Ep0Start() {
  fbl::AutoLock lock(&ep0_.lock);

  CmdStartNewConfig(ep0_.out, 0);
  EpSetConfig(ep0_.out, true);
  EpSetConfig(ep0_.in, true);

  Ep0QueueSetupLocked();
}

void Dwc3::Ep0QueueSetupLocked() {
  ep0_.buffer.CacheFlushInvalidate(0, sizeof(usb_setup_t));
  EpStartTransfer(ep0_.out, ep0_.shared_fifo, TRB_TRBCTL_SETUP, ep0_.buffer.phys(),
                  sizeof(usb_setup_t), false);
  ep0_.state = Ep0::State::Setup;
}

void Dwc3::Ep0StartEndpoints() {
  zxlogf(DEBUG, "Dwc3::Ep0StartEndpoints");

  ep0_.in.type = USB_ENDPOINT_CONTROL;
  ep0_.in.interval = 0;
  CmdEpSetConfig(ep0_.in, true);

  // TODO(johngro): Why do we pass a hardcoded value of 2 for the resource ID
  // here?  Eventually, it is going to end up in the Params field of the DEPCMD
  // (Device EndPoint Command) register, where according to DWC docs (Table
  // 1-102), it will be ignored by the Start New Configuration command we are
  // sending.
  CmdStartNewConfig(ep0_.out, 2);

  for (UserEndpoint& uep : user_endpoints_) {
    fbl::AutoLock lock(&uep.lock);

    if (uep.ep.enabled) {
      EpSetConfig(uep.ep, true);
      UserEpQueueNext(uep);
    }
  }
}

void Dwc3::HandleEp0TransferCompleteEvent(uint8_t ep_num) {
  fbl::AutoLock lock(&ep0_.lock);
  ZX_DEBUG_ASSERT(is_ep0_num(ep_num));

  switch (ep0_.state) {
    case Ep0::State::Setup: {
      void* const vaddr = ep0_.buffer.virt();
      const zx_paddr_t paddr = ep0_.buffer.phys();
      usb_setup_t setup = ep0_.cur_setup;
      //
      memcpy(&setup, vaddr, sizeof(setup));

      zxlogf(DEBUG, "got setup: type: 0x%02X req: %d value: %d index: %d length: %d",
             setup.bm_request_type, setup.b_request, setup.w_value, setup.w_index, setup.w_length);

      const bool is_out = ((setup.bm_request_type & USB_DIR_MASK) == USB_DIR_OUT);
      if (setup.w_length > 0 && is_out) {
        ep0_.buffer.CacheFlushInvalidate(0, ep0_.buffer.size());
        EpStartTransfer(ep0_.out, ep0_.shared_fifo, TRB_TRBCTL_CONTROL_DATA, paddr,
                        ep0_.buffer.size(), false);
        ep0_.state = Ep0::State::DataOut;
      } else {
        zx::status<size_t> status = HandleEp0Setup(setup, vaddr, ep0_.buffer.size());
        if (status.is_error()) {
          zxlogf(DEBUG, "HandleSetup returned %s", zx_status_get_string(status.error_value()));
          CmdEpSetStall(ep0_.out);
          Ep0QueueSetupLocked();
          break;
        }

        const size_t actual = status.value();
        zxlogf(DEBUG, "HandleSetup success: actual %zu", actual);
        if (setup.w_length > 0) {
          // queue a write for the data phase
          ep0_.buffer.CacheFlush(0, actual);
          EpStartTransfer(ep0_.in, ep0_.shared_fifo, TRB_TRBCTL_CONTROL_DATA, paddr, actual, false);
          ep0_.state = Ep0::State::DataIn;
        } else {
          ep0_.state = Ep0::State::WaitNrdyIn;
        }
      }
      break;
    }
    case Ep0::State::DataOut: {
      ZX_DEBUG_ASSERT(ep_num == kEp0Out);

      dwc3_trb_t trb;
      EpReadTrb(ep0_.out, ep0_.shared_fifo, ep0_.shared_fifo.current, &trb);
      ep0_.shared_fifo.current = nullptr;
      zx_off_t received = ep0_.buffer.size() - TRB_BUFSIZ(trb.status);

      zx::status<size_t> status = HandleEp0Setup(ep0_.cur_setup, ep0_.buffer.virt(), received);
      if (status.is_error()) {
        CmdEpSetStall(ep0_.out);
        Ep0QueueSetupLocked();
        break;
      }
      ep0_.state = Ep0::State::WaitNrdyIn;
      break;
    }
    case Ep0::State::DataIn:
      ZX_DEBUG_ASSERT(ep_num == kEp0In);
      ep0_.state = Ep0::State::WaitNrdyOut;
      break;
    case Ep0::State::Status:
      Ep0QueueSetupLocked();
      break;
    default:
      break;
  }
}

void Dwc3::HandleEp0TransferNotReadyEvent(uint8_t ep_num, uint32_t stage) {
  fbl::AutoLock lock(&ep0_.lock);
  ZX_DEBUG_ASSERT(is_ep0_num(ep_num));

  switch (ep0_.state) {
    case Ep0::State::Setup:
      if ((stage == DEPEVT_XFER_NOT_READY_STAGE_DATA) ||
          (stage == DEPEVT_XFER_NOT_READY_STAGE_STATUS)) {
        // Stall if we receive xfer not ready data/status while waiting for setup to complete
        CmdEpSetStall(ep0_.out);
        Ep0QueueSetupLocked();
      }
      break;
    case Ep0::State::DataOut:
      if ((ep_num == kEp0In) && (stage == DEPEVT_XFER_NOT_READY_STAGE_DATA)) {
        // end transfer and stall if we receive xfer not ready in the opposite direction
        CmdEpEndTransfer(ep0_.out);
        CmdEpSetStall(ep0_.in);
        Ep0QueueSetupLocked();
      }
      break;
    case Ep0::State::DataIn:
      if ((ep_num == kEp0Out) && (stage == DEPEVT_XFER_NOT_READY_STAGE_DATA)) {
        // end transfer and stall if we receive xfer not ready in the opposite direction
        CmdEpEndTransfer(ep0_.in);
        CmdEpSetStall(ep0_.out);
        Ep0QueueSetupLocked();
      }
      break;
    case Ep0::State::WaitNrdyOut:
      if (ep_num == kEp0Out) {
        if (ep0_.cur_setup.w_length > 0) {
          EpStartTransfer(ep0_.out, ep0_.shared_fifo, TRB_TRBCTL_STATUS_3, 0, 0, false);
        } else {
          EpStartTransfer(ep0_.out, ep0_.shared_fifo, TRB_TRBCTL_STATUS_2, 0, 0, false);
        }
        ep0_.state = Ep0::State::Status;
      }
      break;
    case Ep0::State::WaitNrdyIn:
      if (ep_num == kEp0In) {
        if (ep0_.cur_setup.w_length > 0) {
          EpStartTransfer(ep0_.in, ep0_.shared_fifo, TRB_TRBCTL_STATUS_3, 0, 0, false);
        } else {
          EpStartTransfer(ep0_.in, ep0_.shared_fifo, TRB_TRBCTL_STATUS_2, 0, 0, false);
        }
        ep0_.state = Ep0::State::Status;
      }
      break;
    case Ep0::State::Status:
    default:
      zxlogf(ERROR, "ready unhandled state %u", static_cast<uint32_t>(ep0_.state));
      break;
  }
}

zx::status<size_t> Dwc3::HandleEp0Setup(const usb_setup_t& setup, void* buffer, size_t length) {
  auto DoControlCall = [this](const usb_setup_t& setup, const uint8_t* in_buf, size_t in_len,
                              uint8_t* out_buf, size_t out_len) -> zx::status<size_t> {
    fbl::AutoLock lock(&dci_lock_);

    if (!dci_intf_.has_value()) {
      return zx::error(ZX_ERR_BAD_STATE);
    }

    size_t actual = 0;
    zx_status_t status = dci_intf_->Control(&setup, in_buf, in_len, out_buf, out_len, &actual);
    if (status == ZX_OK) {
      return zx::ok(actual);
    } else {
      return zx::error(status);
    }
  };

  if (setup.bm_request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) {
    // handle some special setup requests in this driver
    switch (setup.b_request) {
      case USB_REQ_SET_ADDRESS: {
        fbl::AutoLock lock{&lock_};
        SetDeviceAddress(setup.w_value);
      }
        return zx::ok(0);
      case USB_REQ_SET_CONFIGURATION: {
        ResetConfiguration();
        configured_ = false;

        zx::status<size_t> status = DoControlCall(setup, nullptr, 0, nullptr, 0);
        if (status.is_ok() && setup.w_value) {
          ZX_DEBUG_ASSERT(status.value() == 0);
          configured_ = true;
          Ep0StartEndpoints();
        }
        return status;
      }
      default:
        // fall through to the common DoControlCall
        break;
    }
  } else if ((setup.bm_request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE)) &&
             (setup.b_request == USB_REQ_SET_INTERFACE)) {
    ResetConfiguration();
    configured_ = false;

    zx::status<size_t> status = DoControlCall(setup, nullptr, 0, nullptr, 0);
    if (status.is_ok()) {
      configured_ = true;
      Ep0StartEndpoints();
    }
    return status;
  }

  if ((setup.bm_request_type & USB_DIR_MASK) == USB_DIR_IN) {
    return DoControlCall(setup, nullptr, 0, reinterpret_cast<uint8_t*>(buffer), length);
  } else {
    return DoControlCall(setup, nullptr, 0, nullptr, 0);
  }
}

}  // namespace dwc3
