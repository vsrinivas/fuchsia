// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc2.h"

#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/syscalls.h>

#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>

#include "usb_dwc_regs.h"

namespace dwc2 {

// Handler for usbreset interrupt.
void Dwc2::HandleReset() {
  auto* mmio = get_mmio();

  zxlogf(SERIAL, "\nRESET");

  ep0_state_ = Ep0State::DISCONNECTED;
  configured_ = false;

  // Clear remote wakeup signalling
  DCTL::Get().ReadFrom(mmio).set_rmtwkupsig(0).WriteTo(mmio);

  for (uint32_t i = 0; i < MAX_EPS_CHANNELS; i++) {
    auto diepctl = DEPCTL::Get(i).ReadFrom(mmio);

    // Disable IN endpoints
    if (diepctl.epena()) {
      diepctl.set_snak(1);
      diepctl.set_epdis(1);
      diepctl.WriteTo(mmio);
    }

    // Clear snak on OUT endpoints
    DEPCTL::Get(i + DWC_EP_OUT_SHIFT).ReadFrom(mmio).set_snak(1).WriteTo(mmio);
  }

  // Flush endpoint zero TX FIFO
  FlushTxFifo(0);

  // Flush the learning queue
  GRSTCTL::Get().FromValue(0).set_intknqflsh(1).WriteTo(mmio);

  // Enable interrupts for only EPO IN and OUT
  DAINTMSK::Get().FromValue((1 << DWC_EP0_IN) | (1 << DWC_EP0_OUT)).WriteTo(mmio);

  // Enable various endpoint specific interrupts
  DOEPMSK::Get()
      .FromValue(0)
      .set_setup(1)
      .set_stsphsercvd(1)
      .set_xfercompl(1)
      .set_ahberr(1)
      .set_epdisabled(1)
      .WriteTo(mmio);
  DIEPMSK::Get()
      .FromValue(0)
      .set_xfercompl(1)
      .set_timeout(1)
      .set_ahberr(1)
      .set_epdisabled(1)
      .WriteTo(mmio);

  // Clear device address
  DCFG::Get().ReadFrom(mmio).set_devaddr(0).WriteTo(mmio);

  SetConnected(false);
}

// Handler for usbsuspend interrupt.
void Dwc2::HandleSuspend() { SetConnected(false); }

// Handler for enumdone interrupt.
void Dwc2::HandleEnumDone() {
  SetConnected(true);

  auto* mmio = get_mmio();

  ep0_state_ = Ep0State::IDLE;

  endpoints_[DWC_EP0_IN].max_packet_size = 64;
  endpoints_[DWC_EP0_OUT].max_packet_size = 64;
  endpoints_[DWC_EP0_IN].phys = static_cast<uint32_t>(ep0_buffer_.phys());
  endpoints_[DWC_EP0_OUT].phys = static_cast<uint32_t>(ep0_buffer_.phys());

  DEPCTL0::Get(DWC_EP0_IN).ReadFrom(mmio).set_mps(DEPCTL0::MPS_64).WriteTo(mmio);
  DEPCTL0::Get(DWC_EP0_OUT).ReadFrom(mmio).set_mps(DEPCTL0::MPS_64).WriteTo(mmio);

  DCTL::Get().ReadFrom(mmio).set_cgnpinnak(1).WriteTo(mmio);

  GUSBCFG::Get().ReadFrom(mmio).set_usbtrdtim(metadata_.usb_turnaround_time).WriteTo(mmio);

  if (dci_intf_) {
    dci_intf_->SetSpeed(USB_SPEED_HIGH);
  }
  StartEp0();
}

// Handler for inepintr interrupt.
void Dwc2::HandleInEpInterrupt() {
  auto* mmio = get_mmio();
  uint8_t ep_num = 0;

  // Read bits indicating which endpoints have inepintr active
  uint32_t ep_bits = DAINT::Get().ReadFrom(mmio).reg_value();
  ep_bits &= DAINTMSK::Get().ReadFrom(mmio).reg_value();
  ep_bits &= DWC_EP_IN_MASK;

  // Acknowledge the endpoint bits
  DAINT::Get().FromValue(DWC_EP_IN_MASK).WriteTo(mmio);

  // Loop through IN endpoints and handle those with interrupt raised
  while (ep_bits) {
    if (ep_bits & 1) {
      auto diepint = DIEPINT::Get(ep_num).ReadFrom(mmio);
      diepint.set_reg_value(diepint.reg_value() & DIEPMSK::Get().ReadFrom(mmio).reg_value());

      if (diepint.xfercompl()) {
        DIEPINT::Get(ep_num).FromValue(0).set_xfercompl(1).WriteTo(mmio);

        if (ep_num == DWC_EP0_IN) {
          HandleEp0TransferComplete();
        } else {
          HandleTransferComplete(ep_num);
          if (diepint.nak()) {
            zxlogf(ERROR, "Unandled interrupt diepint.nak ep_num %u", ep_num);
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_nak(1).WriteTo(mmio);
          }
        }
      }

      // TODO(voydanoff) Implement error recovery for these interrupts
      if (diepint.epdisabled()) {
        zxlogf(ERROR, "Unandled interrupt diepint.epdisabled for ep_num %u", ep_num);
        DIEPINT::Get(ep_num).ReadFrom(mmio).set_epdisabled(1).WriteTo(mmio);
      }
      if (diepint.ahberr()) {
        zxlogf(ERROR, "Unandled interrupt diepint.ahberr for ep_num %u", ep_num);
        DIEPINT::Get(ep_num).ReadFrom(mmio).set_ahberr(1).WriteTo(mmio);
      }
      if (diepint.timeout()) {
        zxlogf(ERROR, "Unandled interrupt diepint.timeout for ep_num %u", ep_num);
        DIEPINT::Get(ep_num).ReadFrom(mmio).set_timeout(1).WriteTo(mmio);
      }
      if (diepint.intktxfemp()) {
        zxlogf(ERROR, "Unandled interrupt diepint.intktxfemp for ep_num %u", ep_num);
        DIEPINT::Get(ep_num).ReadFrom(mmio).set_intktxfemp(1).WriteTo(mmio);
      }
      if (diepint.intknepmis()) {
        zxlogf(ERROR, "Unhandled interrupt diepint.intknepmis for ep_num %u", ep_num);
        DIEPINT::Get(ep_num).ReadFrom(mmio).set_intknepmis(1).WriteTo(mmio);
      }
      if (diepint.inepnakeff()) {
        printf("Unandled interrupt diepint.inepnakeff for ep_num %u\n", ep_num);
        DIEPINT::Get(ep_num).ReadFrom(mmio).set_inepnakeff(1).WriteTo(mmio);
      }
    }
    ep_num++;
    ep_bits >>= 1;
  }
}

// Handler for outepintr interrupt.
void Dwc2::HandleOutEpInterrupt() {
  auto* mmio = get_mmio();

  uint8_t ep_num = DWC_EP0_OUT;

  // Read bits indicating which endpoints have outepintr active
  auto ep_bits = DAINT::Get().ReadFrom(mmio).reg_value();
  auto ep_mask = DAINTMSK::Get().ReadFrom(mmio).reg_value();
  ep_bits &= ep_mask;
  ep_bits &= DWC_EP_OUT_MASK;
  ep_bits >>= DWC_EP_OUT_SHIFT;

  // Acknowledge the endpoint bits
  DAINT::Get().FromValue(DWC_EP_OUT_MASK).WriteTo(mmio);

  // Loop through OUT endpoints and handle those with interrupt raised
  while (ep_bits) {
    if (ep_bits & 1) {
      auto doepint = DOEPINT::Get(ep_num).ReadFrom(mmio);
      doepint.set_reg_value(doepint.reg_value() & DOEPMSK::Get().ReadFrom(mmio).reg_value());

      if (doepint.sr()) {
        DOEPINT::Get(ep_num).ReadFrom(mmio).set_sr(1).WriteTo(mmio);
      }

      if (doepint.stsphsercvd()) {
        DOEPINT::Get(ep_num).ReadFrom(mmio).set_stsphsercvd(1).WriteTo(mmio);
      }

      if (doepint.setup()) {
        // TODO(voydanoff):   On this interrupt, the application must read the DOEPTSIZn
        // register to determine the number of SETUP packets received and process the last
        // received SETUP packet.
        DOEPINT::Get(ep_num).ReadFrom(mmio).set_setup(1).WriteTo(mmio);

        memcpy(&cur_setup_, ep0_buffer_.virt(), sizeof(cur_setup_));
        zxlogf(SERIAL,
               "SETUP bmRequestType: 0x%02x bRequest: %u wValue: %u wIndex: %u "
               "wLength: %u\n",
               cur_setup_.bmRequestType, cur_setup_.bRequest, cur_setup_.wValue, cur_setup_.wIndex,
               cur_setup_.wLength);

        HandleEp0Setup();
      }
      if (doepint.xfercompl()) {
        DOEPINT::Get(ep_num).FromValue(0).set_xfercompl(1).WriteTo(mmio);

        if (ep_num == DWC_EP0_OUT) {
          if (!doepint.setup()) {
            HandleEp0TransferComplete();
          }
        } else {
          HandleTransferComplete(ep_num);
        }
      }
      // TODO(voydanoff) Implement error recovery for these interrupts
      if (doepint.epdisabled()) {
        zxlogf(ERROR, "Unhandled interrupt doepint.epdisabled for ep_num %u", ep_num);
        DOEPINT::Get(ep_num).ReadFrom(mmio).set_epdisabled(1).WriteTo(mmio);
      }
      if (doepint.ahberr()) {
        zxlogf(ERROR, "Unhandled interrupt doepint.ahberr for ep_num %u", ep_num);
        DOEPINT::Get(ep_num).ReadFrom(mmio).set_ahberr(1).WriteTo(mmio);
      }
    }
    ep_num++;
    ep_bits >>= 1;
  }
}

// Handles setup requests from the host.
zx_status_t Dwc2::HandleSetupRequest(size_t* out_actual) {
  zx_status_t status;

  auto* setup = &cur_setup_;
  auto* buffer = ep0_buffer_.virt();
  zx::duration elapsed;
  zx::time now;
  if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) {
    // Handle some special setup requests in this driver
    switch (setup->bRequest) {
      case USB_REQ_SET_ADDRESS:
        zxlogf(SERIAL, "SET_ADDRESS %d", setup->wValue);
        SetAddress(static_cast<uint8_t>(setup->wValue));
        now = zx::clock::get_monotonic();
        elapsed = now - irq_timestamp_;
        zxlogf(
            INFO,
            "Took %i microseconds to reply to SET_ADDRESS interrupt\nStarted waiting at %lx\nGot "
            "hardware IRQ at %lx\nFinished processing at %lx, context switch happened at %lx",
            static_cast<int>(elapsed.to_usecs()), wait_start_time_.get(), irq_timestamp_.get(),
            now.get(), irq_dispatch_timestamp_.get());
        if (elapsed.to_msecs() > 2) {
          zxlogf(ERROR, "Handling SET_ADDRESS took greater than 2ms");
        }
        *out_actual = 0;
        return ZX_OK;
      case USB_REQ_SET_CONFIGURATION:
        zxlogf(SERIAL, "SET_CONFIGURATION %d", setup->wValue);
        configured_ = true;
        if (dci_intf_) {
          status = dci_intf_->Control(setup, nullptr, 0, nullptr, 0, out_actual);
        } else {
          status = ZX_ERR_NOT_SUPPORTED;
        }
        if (status == ZX_OK && setup->wValue) {
          StartEndpoints();
        } else {
          configured_ = false;
        }
        return status;
      default:
        // fall through to dci_intf_->Control()
        break;
    }
  }

  bool is_in = ((setup->bmRequestType & USB_DIR_MASK) == USB_DIR_IN);
  auto length = le16toh(setup->wLength);

  if (dci_intf_) {
    if (length == 0) {
      status = dci_intf_->Control(setup, nullptr, 0, nullptr, 0, out_actual);
    } else if (is_in) {
      status = dci_intf_->Control(setup, nullptr, 0, buffer, length, out_actual);
    } else {
      status = ZX_ERR_NOT_SUPPORTED;
    }
  } else {
    status = ZX_ERR_NOT_SUPPORTED;
  }
  if (status == ZX_OK) {
    auto* ep = &endpoints_[DWC_EP0_OUT];
    ep->req_offset = 0;
    if (is_in) {
      ep->req_length = static_cast<uint32_t>(*out_actual);
    }
  }
  return status;
}

// Programs the device address received from the SET_ADDRESS command from the host
void Dwc2::SetAddress(uint8_t address) {
  auto* mmio = get_mmio();

  DCFG::Get().ReadFrom(mmio).set_devaddr(address).WriteTo(mmio);
}

// Reads number of bytes transfered on specified endpoint
uint32_t Dwc2::ReadTransfered(Endpoint* ep) {
  auto* mmio = get_mmio();
  return ep->req_xfersize - DEPTSIZ::Get(ep->ep_num).ReadFrom(mmio).xfersize();
}

// Prepares to receive next control request on endpoint zero.
void Dwc2::StartEp0() {
  auto* mmio = get_mmio();
  auto* ep = &endpoints_[DWC_EP0_OUT];
  ep->req_offset = 0;
  ep->req_xfersize = 3 * sizeof(usb_setup_t);

  ep0_buffer_.CacheFlushInvalidate(0, sizeof(cur_setup_));

  DEPDMA::Get(DWC_EP0_OUT)
      .FromValue(0)
      .set_addr(static_cast<uint32_t>(ep0_buffer_.phys()))
      .WriteTo(get_mmio());

  DEPTSIZ0::Get(DWC_EP0_OUT)
      .FromValue(0)
      .set_supcnt(3)
      .set_pktcnt(1)
      .set_xfersize(ep->req_xfersize)
      .WriteTo(mmio);
  hw_wmb();

  DEPCTL::Get(DWC_EP0_OUT).ReadFrom(mmio).set_epena(1).WriteTo(mmio);
  hw_wmb();
}

// Queues the next USB request for the specified endpoint
void Dwc2::QueueNextRequest(Endpoint* ep) {
  std::optional<Request> req;
  if (ep->current_req == nullptr) {
    req = ep->queued_reqs.pop();
  }

  if (req.has_value()) {
    auto* usb_req = req->take();
    ep->current_req = usb_req;

    phys_iter_t iter;
    zx_paddr_t phys;
    usb_request_physmap(usb_req, bti_.get());
    usb_request_phys_iter_init(&iter, usb_req, PAGE_SIZE);
    usb_request_phys_iter_next(&iter, &phys);
    ep->phys = static_cast<uint32_t>(phys);

    ep->req_offset = 0;
    ep->req_length = static_cast<uint32_t>(usb_req->header.length);
    StartTransfer(ep, ep->req_length);
  }
}

void Dwc2::StartTransfer(Endpoint* ep, uint32_t length) {
  auto ep_num = ep->ep_num;
  auto* mmio = get_mmio();
  bool is_in = DWC_EP_IS_IN(ep_num);

  if (length > 0) {
    if (is_in) {
      if (ep_num == DWC_EP0_IN) {
        ep0_buffer_.CacheFlush(ep->req_offset, length);
      } else {
        usb_request_cache_flush(ep->current_req, ep->req_offset, length);
      }
    } else {
      if (ep_num == DWC_EP0_OUT) {
        ep0_buffer_.CacheFlushInvalidate(ep->req_offset, length);
      } else {
        usb_request_cache_flush_invalidate(ep->current_req, ep->req_offset, length);
      }
    }
  }

  // Program DMA address
  DEPDMA::Get(ep_num).FromValue(0).set_addr(ep->phys + ep->req_offset).WriteTo(mmio);

  uint32_t ep_mps = ep->max_packet_size;
  auto deptsiz = DEPTSIZ::Get(ep_num).FromValue(0);

  if (length == 0) {
    deptsiz.set_xfersize(is_in ? 0 : ep_mps);
    deptsiz.set_pktcnt(1);
  } else {
    deptsiz.set_pktcnt((length + (ep_mps - 1)) / ep_mps);
    deptsiz.set_xfersize(length);
  }
  deptsiz.set_mc(is_in ? 1 : 0);
  ep->req_xfersize = deptsiz.xfersize();
  deptsiz.WriteTo(mmio);
  hw_wmb();

  DEPCTL::Get(ep_num).ReadFrom(mmio).set_cnak(1).set_epena(1).WriteTo(mmio);
  hw_wmb();
}

void Dwc2::FlushTxFifo(uint32_t fifo_num) {
  auto* mmio = get_mmio();

  auto grstctl = GRSTCTL::Get().FromValue(0).set_txfflsh(1).set_txfnum(fifo_num).WriteTo(mmio);

  uint32_t count = 0;
  do {
    grstctl.ReadFrom(mmio);
    // Retry count of 10000 comes from Amlogic bootloader driver.
    if (++count > 10000)
      break;
  } while (grstctl.txfflsh() == 1);

  zx::nanosleep(zx::deadline_after(zx::usec(1)));
}

void Dwc2::FlushRxFifo() {
  auto* mmio = get_mmio();
  auto grstctl = GRSTCTL::Get().FromValue(0).set_rxfflsh(1).WriteTo(mmio);

  uint32_t count = 0;
  do {
    grstctl.ReadFrom(mmio);
    if (++count > 10000)
      break;
  } while (grstctl.rxfflsh() == 1);

  zx::nanosleep(zx::deadline_after(zx::usec(1)));
}

void Dwc2::FlushTxFifoRetryIndefinite(uint32_t fifo_num) {
  auto* mmio = get_mmio();

  auto grstctl = GRSTCTL::Get().FromValue(0).set_txfflsh(1).set_txfnum(fifo_num).WriteTo(mmio);

  do {
    grstctl.ReadFrom(mmio);
  } while (grstctl.txfflsh() == 1);

  zx::nanosleep(zx::deadline_after(zx::usec(1)));
}

void Dwc2::FlushRxFifoRetryIndefinite() {
  auto* mmio = get_mmio();
  auto grstctl = GRSTCTL::Get().FromValue(0).set_rxfflsh(1).WriteTo(mmio);

  do {
    grstctl.ReadFrom(mmio);
  } while (grstctl.rxfflsh() == 1);

  zx::nanosleep(zx::deadline_after(zx::usec(1)));
}

void Dwc2::StartEndpoints() {
  for (uint8_t ep_num = 1; ep_num < std::size(endpoints_); ep_num++) {
    auto* ep = &endpoints_[ep_num];
    if (ep->enabled) {
      EnableEp(ep_num, true);

      fbl::AutoLock lock(&ep->lock);
      QueueNextRequest(ep);
    }
  }
}

void Dwc2::EnableEp(uint8_t ep_num, bool enable) {
  auto* mmio = get_mmio();

  fbl::AutoLock lock(&lock_);

  uint32_t bit = 1 << ep_num;

  auto mask = DAINTMSK::Get().ReadFrom(mmio).reg_value();
  if (enable) {
    auto daint = DAINT::Get().ReadFrom(mmio).reg_value();
    daint |= bit;
    DAINT::Get().FromValue(daint).WriteTo(mmio);
    mask |= bit;
  } else {
    mask &= ~bit;
  }
  DAINTMSK::Get().FromValue(mask).WriteTo(mmio);
}

void Dwc2::HandleEp0Setup() {
  auto* setup = &cur_setup_;

  auto length = letoh16(setup->wLength);
  bool is_in = ((setup->bmRequestType & USB_DIR_MASK) == USB_DIR_IN);
  size_t actual = 0;

  // No data to read, can handle setup now
  if (length == 0 || is_in) {
    // TODO(voydanoff) stall if this fails (after we implement stalling)
    __UNUSED zx_status_t _ = HandleSetupRequest(&actual);
  }

  if (length > 0) {
    if (is_in) {
      ep0_state_ = Ep0State::DATA_IN;
      // send data in
      auto* ep = &endpoints_[DWC_EP0_IN];
      ep->req_offset = 0;
      ep->req_length = static_cast<uint32_t>(actual);
      fbl::AutoLock al(&ep->lock);
      StartTransfer(ep, (ep->req_length > 127 ? ep->max_packet_size : ep->req_length));
    } else {
      ep0_state_ = Ep0State::DATA_OUT;
      // queue a read for the data phase
      ep0_state_ = Ep0State::DATA_OUT;
      auto* ep = &endpoints_[DWC_EP0_OUT];
      ep->req_offset = 0;
      ep->req_length = length;
      fbl::AutoLock al(&ep->lock);
      StartTransfer(ep, (length > 127 ? ep->max_packet_size : length));
    }
  } else {
    // no data phase
    // status in IN direction
    HandleEp0Status(true);
  }
}

// Handles status phase of a setup request
void Dwc2::HandleEp0Status(bool is_in) {
  ep0_state_ = (is_in ? Ep0State::STATUS_IN : Ep0State::STATUS_OUT);
  uint8_t ep_num = (is_in ? DWC_EP0_IN : DWC_EP0_OUT);
  auto* ep = &endpoints_[ep_num];
  fbl::AutoLock al(&ep->lock);
  StartTransfer(ep, 0);

  if (is_in) {
    StartEp0();
  }
}

// Handles transfer complete events for endpoint zero
void Dwc2::HandleEp0TransferComplete() {
  switch (ep0_state_) {
    case Ep0State::IDLE: {
      StartEp0();
      break;
    }
    case Ep0State::DATA_IN: {
      auto* ep = &endpoints_[DWC_EP0_IN];
      auto transfered = ReadTransfered(ep);
      ep->req_offset += transfered;

      if (ep->req_offset == ep->req_length) {
        HandleEp0Status(false);
      } else {
        auto length = ep->req_length - ep->req_offset;
        if (length > 64) {
          length = 64;
        }
        fbl::AutoLock al(&ep->lock);
        StartTransfer(ep, length);
      }
      break;
    }
    case Ep0State::DATA_OUT: {
      auto* ep = &endpoints_[DWC_EP0_OUT];
      auto transfered = ReadTransfered(ep);
      ep->req_offset += transfered;

      if (ep->req_offset == ep->req_length) {
        if (dci_intf_) {
          size_t actual;
          dci_intf_->Control(&cur_setup_, (uint8_t*)ep0_buffer_.virt(), ep->req_length, nullptr, 0,
                             &actual);
        }
        HandleEp0Status(true);
      } else {
        auto length = ep->req_length - ep->req_offset;
        // Strangely, the controller can transfer up to 127 bytes in a single transaction.
        // But if length is > 127, the transfer must be done in multiple chunks, and those
        // chunks must be 64 bytes long.
        if (length > 127) {
          length = 64;
        }
        fbl::AutoLock al(&ep->lock);
        StartTransfer(ep, length);
      }
      break;
    }
    case Ep0State::STATUS_OUT:
      ep0_state_ = Ep0State::IDLE;
      StartEp0();
      break;
    case Ep0State::STATUS_IN:
      ep0_state_ = Ep0State::IDLE;
      break;
    case Ep0State::STALL:
    default:
      zxlogf(ERROR, "EP0 state is %d, should not get here", static_cast<int>(ep0_state_));
      break;
  }
}

// Handles transfer complete events for endpoints other than endpoint zero
void Dwc2::HandleTransferComplete(uint8_t ep_num) {
  ZX_DEBUG_ASSERT(ep_num != DWC_EP0_IN && ep_num != DWC_EP0_OUT);
  auto* ep = &endpoints_[ep_num];

  ep->lock.Acquire();

  auto transfered = ReadTransfered(ep);

  ep->req_offset += transfered;

  usb_request_t* req = ep->current_req;
  if (req) {
    Request request(req, sizeof(usb_request_t));
    // It is necessary to set current_req = nullptr
    // in order to make this re-entrant safe and thread-safe.
    // When we call request.Complete the callee may immediately re-queue this request.
    // if it is already in current_req it could be completed twice (since QueueNextRequest
    // would attempt to re-queue it, or CancelAll could take the lock on a separate thread and
    // forcefully complete it after we've already completed it).
    ep->current_req = nullptr;
    ep->lock.Release();
    request.Complete(ZX_OK, ep->req_offset);
    ep->lock.Acquire();

    QueueNextRequest(ep);
  }
  ep->lock.Release();
}

zx_status_t Dwc2::InitController() {
  auto* mmio = get_mmio();

  auto gsnpsid = GSNPSID::Get().ReadFrom(mmio).reg_value();
  if (gsnpsid != 0x4f54400a && gsnpsid != 0x4f54330a) {
    zxlogf(WARNING,
           "DWC2 driver has not been tested with IP version 0x%08x. "
           "The IP has quirks, so things may not work as expected\n",
           gsnpsid);
  }

  auto ghwcfg2 = GHWCFG2::Get().ReadFrom(mmio);
  if (!ghwcfg2.dynamic_fifo()) {
    zxlogf(ERROR, "DWC2 driver requires dynamic FIFO support");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto ghwcfg4 = GHWCFG4::Get().ReadFrom(mmio);
  if (!ghwcfg4.ded_fifo_en()) {
    zxlogf(ERROR, "DWC2 driver requires dedicated FIFO support");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto grstctl = GRSTCTL::Get();
  while (grstctl.ReadFrom(mmio).ahbidle() == 0) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }

  // Reset the controller
  grstctl.FromValue(0).set_csftrst(1).WriteTo(mmio);

  // Wait for reset to complete
  bool done = false;
  for (int i = 0; i < 1000; i++) {
    if (grstctl.ReadFrom(mmio).csftrst() == 0) {
      zx::nanosleep(zx::deadline_after(zx::msec(10)));
      done = true;
      break;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
  if (!done) {
    return ZX_ERR_TIMED_OUT;
  }

  zx::nanosleep(zx::deadline_after(zx::msec(10)));

  // Enable DMA
  GAHBCFG::Get()
      .FromValue(0)
      .set_dmaenable(1)
      .set_hburstlen(metadata_.dma_burst_len)
      .set_nptxfemplvl_txfemplvl(1)
      .WriteTo(mmio);

  // Set turnaround time based on metadata
  GUSBCFG::Get().ReadFrom(mmio).set_usbtrdtim(metadata_.usb_turnaround_time).WriteTo(mmio);
  DCFG::Get()
      .ReadFrom(mmio)
      .set_devaddr(0)
      .set_epmscnt(2)
      .set_descdma(0)
      .set_devspd(0)
      .set_perfrint(DCFG::PERCENT_80)
      .WriteTo(mmio);

  DCTL::Get().ReadFrom(mmio).set_sftdiscon(1).WriteTo(mmio);
  DCTL::Get().ReadFrom(mmio).set_sftdiscon(0).WriteTo(mmio);

  // Reset phy clock
  PCGCCTL::Get().FromValue(0).WriteTo(mmio);

  // Set fifo sizes based on metadata.
  GRXFSIZ::Get().FromValue(0).set_size(metadata_.rx_fifo_size).WriteTo(mmio);
  GNPTXFSIZ::Get()
      .FromValue(0)
      .set_depth(metadata_.nptx_fifo_size)
      .set_startaddr(metadata_.rx_fifo_size)
      .WriteTo(mmio);

  auto fifo_base = metadata_.rx_fifo_size + metadata_.nptx_fifo_size;
  auto dfifo_end = GHWCFG3::Get().ReadFrom(mmio).dfifo_depth();

  for (uint32_t i = 0; i < std::size(metadata_.tx_fifo_sizes); i++) {
    auto fifo_size = metadata_.tx_fifo_sizes[i];

    DTXFSIZ::Get(i + 1).FromValue(0).set_startaddr(fifo_base).set_depth(fifo_size).WriteTo(mmio);
    fifo_base += fifo_size;
  }

  GDFIFOCFG::Get().FromValue(0).set_gdfifocfg(dfifo_end).set_epinfobase(fifo_base).WriteTo(mmio);

  // Flush all FIFOs
  FlushTxFifo(0x10);
  FlushRxFifo();

  GRSTCTL::Get().FromValue(0).set_intknqflsh(1).WriteTo(mmio);

  // Clear all pending device interrupts
  DIEPMSK::Get().FromValue(0).WriteTo(mmio);
  DOEPMSK::Get().FromValue(0).WriteTo(mmio);
  DAINT::Get().FromValue(0xFFFFFFFF).WriteTo(mmio);
  DAINTMSK::Get().FromValue(0).WriteTo(mmio);

  for (uint32_t i = 0; i < DWC_MAX_EPS; i++) {
    DEPCTL::Get(i).FromValue(0).WriteTo(mmio);
    DEPTSIZ::Get(i).FromValue(0).WriteTo(mmio);
  }

  // Clear all pending OTG and global interrupts
  GOTGINT::Get().FromValue(0xFFFFFFFF).WriteTo(mmio);
  GINTSTS::Get().FromValue(0xFFFFFFFF).WriteTo(mmio);

  // Enable selected global interrupts
  GINTMSK::Get()
      .FromValue(0)
      .set_usbreset(1)
      .set_enumdone(1)
      .set_inepintr(1)
      .set_outepintr(1)
      .set_usbsuspend(1)
      .set_erlysuspend(1)
      .WriteTo(mmio);

  // Enable global interrupts
  GAHBCFG::Get().ReadFrom(mmio).set_glblintrmsk(1).WriteTo(mmio);

  return ZX_OK;
}

void Dwc2::SetConnected(bool connected) {
  if (connected == connected_) {
    return;
  }

  if (dci_intf_) {
    dci_intf_->SetConnected(connected);
  }
  if (usb_phy_) {
    usb_phy_->ConnectStatusChanged(connected);
  }

  if (!connected) {
    // Complete any pending requests
    RequestQueue complete_reqs;

    for (uint8_t i = 0; i < std::size(endpoints_); i++) {
      auto* ep = &endpoints_[i];

      fbl::AutoLock lock(&ep->lock);
      if (ep->current_req) {
        complete_reqs.push(Request(ep->current_req, sizeof(usb_request_t)));
        ep->current_req = nullptr;
      }
      for (auto req = ep->queued_reqs.pop(); req; req = ep->queued_reqs.pop()) {
        complete_reqs.push(std::move(*req));
      }

      ep->enabled = false;
    }

    // Requests must be completed outside of the lock.
    for (auto req = complete_reqs.pop(); req; req = complete_reqs.pop()) {
      req->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
    }
  }

  connected_ = connected;
}

zx_status_t Dwc2::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<Dwc2>(parent);
  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t Dwc2::Init() {
  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    zxlogf(ERROR, "Dwc2::Create could not get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_ = ddk::PDev(composite);
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "Dwc2::Create: could not get platform device protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // USB PHY protocol is optional.
  usb_phy_ = ddk::UsbPhyProtocolClient(composite, "dwc2-phy");
  if (!usb_phy_->is_valid()) {
    usb_phy_.reset();
  }

  for (uint8_t i = 0; i < std::size(endpoints_); i++) {
    auto* ep = &endpoints_[i];
    ep->ep_num = i;
  }

  size_t actual;
  auto status = DdkGetMetadata(DEVICE_METADATA_PRIVATE, &metadata_, sizeof(metadata_), &actual);
  if (status != ZX_OK || actual != sizeof(metadata_)) {
    zxlogf(ERROR, "Dwc2::Init can't get driver metadata");
    return ZX_ERR_INTERNAL;
  }

  status = pdev_.MapMmio(0, &mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dwc2::Init MapMmio failed: %d", status);
    return status;
  }

  status = pdev_.GetInterrupt(0, &irq_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dwc2::Init GetInterrupt failed: %d", status);
    return status;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dwc2::Init GetBti failed: %d", status);
    return status;
  }

  status = ep0_buffer_.Init(bti_.get(), UINT16_MAX, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dwc2::Init ep0_buffer_.Init failed: %d", status);
    return status;
  }

  status = ep0_buffer_.PhysMap();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dwc2::Init ep0_buffer_.PhysMap failed: %d", status);
    return status;
  }

  if ((status = InitController()) != ZX_OK) {
    zxlogf(ERROR, "Dwc2::Init InitController failed: %d", status);
    return status;
  }

  status = DdkAdd("dwc2");
  if (status != ZX_OK) {
    zxlogf(ERROR, "Dwc2::Init DdkAdd failed: %d", status);
    return status;
  }

  return ZX_OK;
}

void Dwc2::DdkInit(ddk::InitTxn txn) {
  int rc = thrd_create_with_name(
      &irq_thread_, [](void* arg) -> int { return reinterpret_cast<Dwc2*>(arg)->IrqThread(); },
      reinterpret_cast<void*>(this), "dwc2-interrupt-thread");
  if (rc == thrd_success) {
    irq_thread_started_ = true;
    txn.Reply(ZX_OK);
  } else {
    txn.Reply(ZX_ERR_INTERNAL);
  }
}

int Dwc2::IrqThread() {
  auto* mmio = get_mmio();

  while (1) {
    wait_start_time_ = zx::clock::get_monotonic();
    auto wait_res = irq_.wait(&irq_timestamp_);
    irq_dispatch_timestamp_ = zx::clock::get_monotonic();
    if (wait_res == ZX_ERR_CANCELED) {
      break;
    } else if (wait_res != ZX_OK) {
      zxlogf(ERROR, "dwc_usb: irq wait failed, retcode = %d", wait_res);
    }

    // It doesn't seem that this inner loop should be necessary,
    // but without it we miss interrupts on some versions of the IP.
    while (1) {
      auto gintsts = GINTSTS::Get().ReadFrom(mmio);
      auto gintmsk = GINTMSK::Get().ReadFrom(mmio);
      gintsts.WriteTo(mmio);
      gintsts.set_reg_value(gintsts.reg_value() & gintmsk.reg_value());

      if (gintsts.reg_value() == 0) {
        break;
      }

      if (gintsts.usbreset()) {
        HandleReset();
      }
      if (gintsts.usbsuspend() || gintsts.erlysuspend()) {
        HandleSuspend();
      }
      if (gintsts.enumdone()) {
        HandleEnumDone();
      }
      if (gintsts.inepintr()) {
        HandleInEpInterrupt();
      }
      if (gintsts.outepintr()) {
        HandleOutEpInterrupt();
      }
    }
  }

  zxlogf(INFO, "dwc_usb: irq thread finished");
  return 0;
}

void Dwc2::DdkUnbind(ddk::UnbindTxn txn) {
  irq_.destroy();
  if (irq_thread_started_) {
    irq_thread_started_ = false;
    thrd_join(irq_thread_, nullptr);
  }
  txn.Reply();
}

void Dwc2::DdkRelease() { delete this; }

void Dwc2::DdkSuspend(ddk::SuspendTxn txn) {
  fbl::AutoLock lock(&lock_);
  irq_.destroy();
  shutting_down_ = true;
  // Disconnect from host to prevent DMA from being started
  DCTL::Get().ReadFrom(&mmio_.value()).set_sftdiscon(1).WriteTo(&mmio_.value());
  auto grstctl = GRSTCTL::Get();
  auto mmio = &mmio_.value();
  // Start soft reset sequence -- I think this should clear the DMA FIFOs
  grstctl.FromValue(0).set_csftrst(1).WriteTo(mmio);

  // Wait for reset to complete
  while (grstctl.ReadFrom(mmio).csftrst()) {
    // Arbitrary sleep to yield our timeslice while we wait for
    // hardware to complete its reset.
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }
  lock.release();

  if (irq_thread_started_) {
    irq_thread_started_ = false;
    thrd_join(irq_thread_, nullptr);
  }
  ep0_buffer_.release();
  txn.Reply(ZX_OK, 0);
}

void Dwc2::UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_t* cb) {
  {
    fbl::AutoLock lock(&lock_);
    if (shutting_down_) {
      lock.release();
      usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0, cb);
    }
  }
  uint8_t ep_num = DWC_ADDR_TO_INDEX(req->header.ep_address);
  if (ep_num == DWC_EP0_IN || ep_num == DWC_EP0_OUT || ep_num >= std::size(endpoints_)) {
    zxlogf(ERROR, "Dwc2::UsbDciRequestQueue: bad ep address 0x%02X", req->header.ep_address);
    usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
    return;
  }
  zxlogf(SERIAL, "UsbDciRequestQueue ep %u length %zu", ep_num, req->header.length);

  auto* ep = &endpoints_[ep_num];

  if (!ep->enabled) {
    usb_request_complete(req, ZX_ERR_BAD_STATE, 0, cb);
    return;
  }

  // OUT transactions must have length > 0 and multiple of max packet size
  if (DWC_EP_IS_OUT(ep_num)) {
    if (req->header.length == 0 || req->header.length % ep->max_packet_size != 0) {
      zxlogf(ERROR, "dwc_ep_queue: OUT transfers must be multiple of max packet size");
      usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
      return;
    }
  }

  fbl::AutoLock lock(&ep->lock);

  if (!ep->enabled) {
    zxlogf(ERROR, "dwc_ep_queue ep not enabled!");
    usb_request_complete(req, ZX_ERR_BAD_STATE, 0, cb);
    return;
  }

  if (!configured_) {
    zxlogf(ERROR, "dwc_ep_queue not configured!");
    usb_request_complete(req, ZX_ERR_BAD_STATE, 0, cb);
    return;
  }

  ep->queued_reqs.push(Request(req, *cb, sizeof(usb_request_t)));
  QueueNextRequest(ep);
}

zx_status_t Dwc2::UsbDciSetInterface(const usb_dci_interface_protocol_t* interface) {
  if (dci_intf_) {
    zxlogf(ERROR, "%s: dci_intf_ already set", __func__);
    return ZX_ERR_BAD_STATE;
  }

  dci_intf_ = ddk::UsbDciInterfaceProtocolClient(interface);

  return ZX_OK;
}

zx_status_t Dwc2::UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                 const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
  auto* mmio = get_mmio();

  uint8_t ep_num = DWC_ADDR_TO_INDEX(ep_desc->bEndpointAddress);
  if (ep_num == DWC_EP0_IN || ep_num == DWC_EP0_OUT || ep_num >= std::size(endpoints_)) {
    zxlogf(ERROR, "Dwc2::UsbDciConfigEp: bad ep address 0x%02X", ep_desc->bEndpointAddress);
    return ZX_ERR_INVALID_ARGS;
  }

  bool is_in = (ep_desc->bEndpointAddress & USB_DIR_MASK) == USB_DIR_IN;
  uint8_t ep_type = usb_ep_type(ep_desc);
  uint16_t max_packet_size = usb_ep_max_packet(ep_desc);

  if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
    zxlogf(ERROR, "Dwc2::UsbDciConfigEp: isochronous endpoints are not supported");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* ep = &endpoints_[ep_num];
  fbl::AutoLock lock(&ep->lock);

  ep->type = ep_type;
  ep->max_packet_size = max_packet_size;
  ep->enabled = true;

  DEPCTL::Get(ep_num)
      .FromValue(0)
      .set_mps(ep->max_packet_size)
      .set_eptype(ep_type)
      .set_setd0pid(1)
      .set_txfnum(is_in ? ep_num : 0)
      .set_usbactep(1)
      .WriteTo(mmio);

  EnableEp(ep_num, true);

  if (configured_) {
    QueueNextRequest(ep);
  }

  return ZX_OK;
}

zx_status_t Dwc2::UsbDciDisableEp(uint8_t ep_address) {
  auto* mmio = get_mmio();

  unsigned ep_num = DWC_ADDR_TO_INDEX(ep_address);
  if (ep_num == DWC_EP0_IN || ep_num == DWC_EP0_OUT || ep_num >= std::size(endpoints_)) {
    zxlogf(ERROR, "Dwc2::UsbDciConfigEp: bad ep address 0x%02X", ep_address);
    return ZX_ERR_INVALID_ARGS;
  }

  auto* ep = &endpoints_[ep_num];

  fbl::AutoLock lock(&ep->lock);

  DEPCTL::Get(ep_num).ReadFrom(mmio).set_usbactep(0).WriteTo(mmio);
  ep->enabled = false;

  return ZX_OK;
}

zx_status_t Dwc2::UsbDciEpSetStall(uint8_t ep_address) {
  // TODO(voydanoff) implement this
  return ZX_OK;
}

zx_status_t Dwc2::UsbDciEpClearStall(uint8_t ep_address) {
  // TODO(voydanoff) implement this
  return ZX_OK;
}

size_t Dwc2::UsbDciGetRequestSize() { return Request::RequestSize(sizeof(usb_request_t)); }

zx_status_t Dwc2::UsbDciCancelAll(uint8_t epid) {
  uint8_t ep_num = DWC_ADDR_TO_INDEX(epid);
  auto* ep = &endpoints_[ep_num];

  fbl::AutoLock lock(&ep->lock);
  if (DWC_EP_IS_OUT(ep_num)) {
    FlushRxFifoRetryIndefinite();
  } else {
    FlushTxFifoRetryIndefinite(ep_num);
  }
  RequestQueue queue = std::move(ep->queued_reqs);
  if (ep->current_req) {
    queue.push(Request(ep->current_req, sizeof(usb_request_t)));
    ep->current_req = nullptr;
  }
  lock.release();
  queue.CompleteAll(ZX_ERR_IO_NOT_PRESENT, 0);
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Dwc2::Create;
  return ops;
}();

}  // namespace dwc2

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(dwc2, dwc2::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
ZIRCON_DRIVER_END(dwc2)
    // clang-format on
