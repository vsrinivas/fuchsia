// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crg-udc.h"

#include <lib/ddk/hw/arch_ops.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "crg_udc_regs.h"
#include "src/devices/usb/drivers/crg-udc/crg_udc_bind.h"

namespace crg_udc {

// Put EP0 in protocol stall state
void CrgUdc::SetEp0Halt() {
  auto* ep = &endpoints_[0];

  if (ep->ep_state == EpState::kEpStateHalted || ep->ep_state == EpState::kEpStateDisabled) {
    return;
  }

  BuildEp0Status(ep, 0, 1);
  ep->ep_state = EpState::kEpStateHalted;
}

// Update dequeue pointer after processing a transfer event
void CrgUdc::UpdateDequeuePt(Endpoint* ep, TRBlock* event) {
  uint32_t deq_pt_lo = event->dw0;
  uint32_t deq_pt_hi = event->dw1;
  uint64_t dq_pt_addr = static_cast<uint64_t>(deq_pt_lo) + (static_cast<uint64_t>(deq_pt_hi) << 32);
  TRBlock* deq_pt = nullptr;
  zx_paddr_t offset = 0;

  offset = TranTrbDmaToVirt(ep, static_cast<zx_paddr_t>(dq_pt_addr));
  deq_pt = ep->first_trb + offset;
  deq_pt++;

  if (((deq_pt->dw3 >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) == TRB_TYPE_LINK) {
    deq_pt = ep->first_trb;
  }
  ep->deq_pt = deq_pt;
}

// Handle the completion status of a transfer event
void CrgUdc::HandleCompletionCode(Endpoint* ep, TRBlock* event) {
  uint64_t trb_pt;
  TRBlock* p_trb = nullptr;
  zx_paddr_t offset = 0;
  uint32_t trb_transfer_length;

  trb_pt = static_cast<uint64_t>(event->dw0) + (static_cast<uint64_t>(event->dw1) << 32);
  offset = TranTrbDmaToVirt(ep, static_cast<zx_paddr_t>(trb_pt));
  p_trb = ep->first_trb + offset;

  if (((p_trb->dw3 >> TRB_CHAIN_BIT_SHIFT) & TRB_CHAIN_BIT_MASK) != 1) {
    // chain bit is not set, which mean it is the end of a TD
    trb_transfer_length = event->dw2 & TRB_TRANSFER_LEN_MASK;
    ep->req_xfersize = ep->req_length - trb_transfer_length;
    HandleTransferComplete(ep->ep_num);

    if (ep->type == USB_ENDPOINT_CONTROL) {
      HandleEp0TransferComplete();
    }
  }
}

// Halt physical EP(s)
void CrgUdc::SetEpHalt(Endpoint* ep) {
  auto* mmio = get_mmio();
  uint32_t param0;
  uint32_t eprunning = 0;

  if (ep->ep_num == 0 || ep->ep_state == EpState::kEpStateDisabled ||
      ep->ep_state == EpState::kEpStateHalted) {
    return;
  }

  param0 = 0x1 << ep->ep_num;
  IssueCmd(CmdType::kCrgCmdSetHalt, param0, 0);
  do {
    eprunning = EPRUN::Get().ReadFrom(mmio).reg_value();
  } while ((eprunning & param0) != 0);

  CompletePendingRequest(ep);

  ep->deq_pt = ep->enq_pt;
  ep->transfer_ring_full = false;
  ep->ep_state = EpState::kEpStateHalted;
}

// Handle transfer event TRB
zx_status_t CrgUdc::HandleXferEvent(TRBlock* event) {
  uint8_t ep_num = (event->dw3 >> EVE_TRB_ENDPOINT_ID_SHIFT) & EVE_TRB_ENDPOINT_ID_MASK;
  auto* ep = &endpoints_[ep_num];
  TrbCmplCode completion_code;
  bool trbs_dequeued = false;

  if (!ep->first_trb || ep->ep_state == EpState::kEpStateDisabled) {
    zxlogf(ERROR, "The endpoint %d not enabled", ep_num);
    return ZX_ERR_NOT_SUPPORTED;
  }

  completion_code =
      static_cast<TrbCmplCode>((event->dw2 >> EVE_TRB_COMPL_CODE_SHIFT) & EVE_TRB_COMPL_CODE_MASK);
  if (completion_code == TrbCmplCode::kCmplCodeStopped ||
      completion_code == TrbCmplCode::kCmplCodeStoppedLengthInvalid ||
      completion_code == TrbCmplCode::kCmplCodeDisabled ||
      completion_code == TrbCmplCode::kCmplCodeDisabledLengthInvalid ||
      completion_code == TrbCmplCode::kCmplCodeHalted ||
      completion_code == TrbCmplCode::kCmplCodeHaltedLengthInvalid) {
    zxlogf(INFO, "completion_code = %d(STOPPED/HALTED/DISABLED)", completion_code);
  } else {
    UpdateDequeuePt(ep, event);
  }

  switch (completion_code) {
    case TrbCmplCode::kCmplCodeSuccess: {
      HandleCompletionCode(ep, event);
      trbs_dequeued = true;
      break;
    }
    case TrbCmplCode::kCmplCodeShortPkt: {
      uint32_t trb_transfer_length;

      if (ep->dir_out) {
        trb_transfer_length = event->dw2 & EVE_TRB_TRAN_LEN_MASK;

        ep->req_xfersize = ep->req_length - trb_transfer_length;
        HandleTransferComplete(ep->ep_num);
      } else {
        zxlogf(INFO, "EP DIR IN");
      }

      trbs_dequeued = true;
      break;
    }
    case TrbCmplCode::kCmplCodeTrbStall: {
      fbl::AutoLock lock(&ep->lock);
      if (ep->current_req != nullptr) {
        usb_request_t* req = ep->current_req;
        Request request(req, sizeof(usb_request_t));
        ep->current_req = nullptr;
        ep->trbs_needed = 0;
        request.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
      }
      trbs_dequeued = true;
      setup_state_ = SetupState::kWaitForSetup;
      break;
    }
    case TrbCmplCode::kCmplCodeSetupTagMismatch: {
      uint32_t enq_idx = ctrl_req_enq_idx_;

      if (ep->deq_pt == ep->enq_pt) {
        fbl::AutoLock lock(&ep->lock);
        if (ep->current_req != nullptr) {
          usb_request_t* req = ep->current_req;
          Request request(req, sizeof(usb_request_t));
          ep->current_req = nullptr;
          request.Complete(ZX_ERR_IO_NOT_PRESENT, 0);
        }

        setup_state_ = SetupState::kWaitForSetup;
        if (enq_idx) {
          SetupPacket* setup_pkt;

          setup_pkt = &ctrl_req_queue_[enq_idx - 1];
          memcpy(&cur_setup_, &setup_pkt->usbctrlreq, sizeof(cur_setup_));
          setup_tag_ = setup_pkt->setup_tag;
          HandleEp0Setup();
          memset(ctrl_req_queue_, 0, sizeof(struct SetupPacket) * CTRL_REQ_QUEUE_DEPTH);
          ctrl_req_enq_idx_ = 0;
        }
      } else {
        zxlogf(DEBUG, "setuptag mismatch skp dpt!=ept");
      }
      break;
    }
    case TrbCmplCode::kCmplCodeBabbleDetectedErr:
    case TrbCmplCode::kCmplCodeInvalidStreamTypeErr:
    case TrbCmplCode::kCmplCodeRingUnderrun:
    case TrbCmplCode::kCmplCodeRingOverrun:
    case TrbCmplCode::kCmplCodeIsochBufferOverrun:
    case TrbCmplCode::kCmplCodeUsbTransErr:
    case TrbCmplCode::kCmplCodeTrbErr: {
      zxlogf(ERROR, "XFER event error, cmpl_code = 0x%x", completion_code);
      SetEpHalt(ep);
      break;
    }
    case TrbCmplCode::kCmplCodeStopped:
    case TrbCmplCode::kCmplCodeStoppedLengthInvalid: {
      zxlogf(ERROR, "STOP, cmpl_code = 0x%x", completion_code);
      break;
    }
    default: {
      zxlogf(INFO, "UNKNOWN cmpl_code = 0x%x", completion_code);
      break;
    }
  }

  // queue the pending trbs
  if (trbs_dequeued && ep->transfer_ring_full) {
    ep->transfer_ring_full = false;
    fbl::AutoLock al(&ep->lock);
    StartTransfer(ep, ep->req_length_left);
  }

  return ZX_OK;
}

// Handle EP0 setup stage
void CrgUdc::HandleEp0Setup() {
  auto* setup = &cur_setup_;
  auto* ep = &endpoints_[0];

  auto length = letoh16(setup->w_length);
  bool is_in = ((setup->bm_request_type & USB_DIR_MASK) == USB_DIR_IN);
  size_t actual = 0;

  // No data to read, can handle setup now
  if (length == 0 || is_in) {
    // TODO(voydanoff) stall if this fails (after we implement stalling)
    __UNUSED zx_status_t _ = HandleSetupRequest(&actual);
  }

  if (length > 0) {
    if (is_in) {
      setup_state_ = SetupState::kDataStageXfer;
      // send data in
      ep->dir_in = true;
      ep->dir_out = false;
      ep->req_offset = 0;
      ep->req_length = static_cast<uint32_t>(actual);
      fbl::AutoLock al(&ep->lock);
      StartTransfer(ep, (ep->req_length > 127 ? ep->max_packet_size : ep->req_length));
    } else {
      // queue a read for the data phase
      setup_state_ = SetupState::kDataStageRecv;
      ep->dir_in = false;
      ep->dir_out = true;
      ep->req_offset = 0;
      ep->req_length = length;
      fbl::AutoLock al(&ep->lock);
      StartTransfer(ep, (length > 127 ? ep->max_packet_size : length));
    }
  } else {
    // no data phase
    // status in IN direction
    BuildEp0Status(ep, set_addr_, 0);
    if (set_addr_ == 1) {
      set_addr_ = 0;
    }
  }
}

// Handles setup requests from the host.
zx_status_t CrgUdc::HandleSetupRequest(size_t* out_actual) {
  zx_status_t status;

  auto* setup = &cur_setup_;
  auto* buffer = ep0_buffer_.virt();
  zx::duration elapsed;
  zx::time now;

  if (setup->bm_request_type == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) {
    // Handle some special setup requests in this driver
    switch (setup->b_request) {
      case USB_REQ_SET_ADDRESS:
        zxlogf(SERIAL, "SET_ADDRESS %d", setup->w_value);
        SetAddress(static_cast<uint8_t>(setup->w_value));
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
        out_actual = 0;
        return ZX_OK;
      case USB_REQ_SET_CONFIGURATION:
        zxlogf(SERIAL, "SET_CONFIGURATION %d", setup->w_value);
        configured_ = true;
        if (device_state_ <= DeviceState::kUsbStateDefault) {
          SetEp0Halt();
          return ZX_ERR_NOT_SUPPORTED;
        }
        if (dci_intf_) {
          status = dci_intf_->Control(setup, nullptr, 0, nullptr, 0, out_actual);
        } else {
          status = ZX_ERR_NOT_SUPPORTED;
        }
        if (status == ZX_OK && setup->w_value) {
          setup_state_ = SetupState::kStatusStageXfer;
          if (device_state_ == DeviceState::kUsbStateAddress) {
            device_state_ = DeviceState::kUsbStateConfigured;
          }
        } else {
          configured_ = false;
        }
        return status;
      default:
        // fall through to dci_intf_->Control()
        break;
    }
  }

  bool is_in = ((setup->bm_request_type & USB_DIR_MASK) == USB_DIR_IN);
  auto length = le16toh(setup->w_length);

  if (dci_intf_) {
    if (length == 0) {
      status = dci_intf_->Control(setup, nullptr, 0, nullptr, 0, out_actual);
    } else if (is_in) {
      status = dci_intf_->Control(setup, nullptr, 0, reinterpret_cast<uint8_t*>(buffer), length,
                                  out_actual);
    } else {
      status = ZX_ERR_NOT_SUPPORTED;
    }
  } else {
    status = ZX_ERR_NOT_SUPPORTED;
  }
  if (status == ZX_OK) {
    auto* ep = &endpoints_[0];
    ep->req_offset = 0;
    if (is_in) {
      ep->req_length = static_cast<uint32_t>(*out_actual);
    }
  }
  return status;
}

// Update device status after setting the address
void CrgUdc::SetAddressCallback() {
  if (device_state_ == DeviceState::kUsbStateDefault && dev_addr_ != 0) {
    device_state_ = DeviceState::kUsbStateAddress;
  } else if (device_state_ == DeviceState::kUsbStateAddress) {
    if (dev_addr_ == 0) {
      device_state_ = DeviceState::kUsbStateDefault;
    }
  }
}

// fill the status stage TRB
void CrgUdc::SetupStatusTrb(TRBlock* p_trb, uint8_t pcs, uint8_t set_addr, uint8_t stall) {
  uint32_t tmp;
  uint32_t dir = 0;

  // Reserved
  p_trb->dw0 = 0;
  p_trb->dw1 = 0;

  // bit[22:31]: interrupt target
  tmp = (0x0 & TRB_INTR_TARGET_MASK) << TRB_INTR_TARGET_SHIFT;
  p_trb->dw2 = tmp;

  // bit0: cycle bit
  // bit5: interrupt on complete
  // bit[10:15]: trb type
  tmp = pcs & TRB_CYCLE_BIT_MASK;
  tmp |= 0x1 << TRB_INTR_ON_COMPLETION_SHIFT;
  tmp |= (TRB_TYPE_XFER_STATUS_STAGE & TRB_TYPE_MASK) << TRB_TYPE_SHIFT;

  // bit16: direction
  // bit[17:18]: setup tag
  // bit19: stall state
  // bit20: set address
  dir = (setup_state_ == SetupState::kStatusStageXfer ? 0 : 1);
  tmp |= (dir & DATA_STAGE_TRB_DIR_MASK) << DATA_STAGE_TRB_DIR_SHIFT;
  tmp |= (setup_tag_ & TRB_SETUP_TAG_MASK) << TRB_SETUP_TAG_SHIFT;
  tmp |= (stall & STATUS_STAGE_TRB_STALL_MASK) << STATUS_STAGE_TRB_STALL_SHIFT;
  tmp |= (set_addr & STATUS_STAGE_TRB_SET_ADDR_MASK) << STATUS_STAGE_TRB_SET_ADDR_SHIFT;
  p_trb->dw3 = tmp;

  // Make sure the TRB was built before starting the DMA transfer
  hw_wmb();
}

// Build the status stage TRB
void CrgUdc::BuildEp0Status(Endpoint* ep, uint8_t set_addr, uint8_t stall) {
  auto* enq_pt = ep->enq_pt;

  SetupStatusTrb(enq_pt, ep->pcs, set_addr, stall);
  enq_pt++;

  if (((enq_pt->dw3 >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) == TRB_TYPE_LINK) {
    if ((enq_pt->dw3 >> TRB_LINK_TOGGLE_CYCLE_SHIFT) & TRB_LINK_TOGGLE_CYCLE_MASK) {
      enq_pt->dw3 &= ~(TRB_CYCLE_BIT_MASK << TRB_CYCLE_BIT_SHIFT);
      enq_pt->dw3 |= (ep->pcs & TRB_CYCLE_BIT_MASK) << TRB_CYCLE_BIT_SHIFT;
      ep->pcs ^= 0x1;
    }
    enq_pt = ep->first_trb;
  }
  ep->enq_pt = enq_pt;
  KnockDoorbell(ep->ep_num);
}

// Programs the device address received from the SET_ADDRESS command from the host
void CrgUdc::SetAddress(uint8_t address) {
  if (((device_state_ == DeviceState::kUsbStateDefault) && address != 0) ||
      (device_state_ == DeviceState::kUsbStateAddress)) {
    uint32_t param0;

    dev_addr_ = address;
    param0 = address & 0xff;
    IssueCmd(CmdType::kCrgCmdSetAddr, param0, 0);
    set_addr_ = 1;
  }

  setup_state_ = SetupState::kStatusStageXfer;
}

// Queues the next USB request for the specified endpoint
void CrgUdc::QueueNextRequest(Endpoint* ep) {
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
    usb_request_phys_iter_init(&iter, usb_req, zx_system_get_page_size());
    usb_request_phys_iter_next(&iter, &phys);
    ep->phys = phys;

    ep->req_offset = 0;
    ep->req_length = static_cast<uint32_t>(usb_req->header.length);
    ep->zlp = usb_req->header.send_zlp;
    StartTransfer(ep, ep->req_length);
  }
}

// Get the free size from the transfer ring
uint32_t CrgUdc::RoomOnRing(uint32_t trbs_num, TRBlock* xfer_ring, TRBlock* enq_pt,
                            TRBlock* dq_pt) {
  uint32_t i = 0;

  if (enq_pt == dq_pt) {
    // ring is empty
    return trbs_num - 1;
  }

  while (enq_pt != dq_pt) {
    i++;
    enq_pt++;

    if (enq_pt->dw3 == TRB_TYPE_LINK) {
      enq_pt = xfer_ring;
    }
    if (i > trbs_num) {
      break;
    }
  }
  return i - 1;
}

// Fill the normal transfer TRB
void CrgUdc::SetupNormalTrb(TRBlock* p_trb, uint32_t xfer_len, uint64_t buf_addr, uint8_t td_size,
                            uint8_t pcs, uint8_t trb_type, uint8_t short_pkt, uint8_t chain_bit,
                            uint8_t intr_on_compl, bool setup_stage, uint8_t usb_dir, bool isoc,
                            uint16_t frame_i_d, uint8_t SIA, uint8_t AZP) {
  uint32_t tmp = 0;

  // Pointing to the start address of data buffer associated with this TRB
  p_trb->dw0 = LOWER_32_BITS(buf_addr);
  p_trb->dw1 = UPPER_32_BITS(buf_addr);

  // bit[0:16]: size of data buffer in bytes
  // bit[17:21]: indicating how many packets still need to be transferred
  tmp = xfer_len & EVE_TRB_TRAN_LEN_MASK;
  tmp |= (td_size & TRB_TD_SIZE_MASK) << TRB_TD_SIZE_SHIFT;
  p_trb->dw2 = tmp;

  // bit0: mark the enqueue pointer of the transfer ring
  // bit2: flag for shot packet
  // bit4: chain bit for the same TD
  // bit5: interrupt on complete
  // bit7: append zero length packet
  // bit[10:15]: TRB type
  tmp = pcs & TRB_CYCLE_BIT_MASK;
  tmp |= (short_pkt & TRB_INTR_ON_SHORT_PKT_MASK) << TRB_INTR_ON_SHORT_PKT_SHIFT;
  tmp |= (chain_bit & TRB_CHAIN_BIT_MASK) << TRB_CHAIN_BIT_SHIFT;
  tmp |= (intr_on_compl & TRB_INTR_ON_COMPLETION_MASK) << TRB_INTR_ON_COMPLETION_SHIFT;
  tmp |= (AZP & TRB_APPEND_ZLP_MASK) << TRB_APPEND_ZLP_SHIFT;
  tmp |= (trb_type & TRB_TYPE_MASK) << TRB_TYPE_SHIFT;

  if (setup_stage) {
    tmp |= (usb_dir & DATA_STAGE_TRB_DIR_MASK) << DATA_STAGE_TRB_DIR_SHIFT;
  }

  if (isoc) {
    tmp |= (frame_i_d & ISOC_TRB_FRAME_ID_MASK) << ISOC_TRB_FRAME_ID_SHIFT;
    tmp |= (SIA & ISOC_TRB_SIA_MASK) << ISOC_TRB_SIA_SHIFT;
  }
  p_trb->dw3 = tmp;
  // Make sure the TRB was built before starting the DMA transfer
  hw_wmb();
}

// Fill the data stage TRB
void CrgUdc::SetupDataStageTrb(Endpoint* ep, TRBlock* p_trb, uint8_t pcs, uint32_t transfer_length,
                               uint32_t td_size, uint8_t IOC, uint8_t AZP, uint8_t dir,
                               uint16_t setup_tag) {
  uint32_t tmp;

  // Pointing to the start address of data buffer associated with this TRB
  p_trb->dw0 = LOWER_32_BITS(static_cast<uint64_t>(ep0_buffer_.phys()));
  p_trb->dw1 = UPPER_32_BITS(static_cast<uint64_t>(ep0_buffer_.phys()));

  // bit[0:16]: size of data buffer in bytes
  // bit[17:21]: indicating how many packets still need to be transferred
  tmp = transfer_length & TRB_TRANSFER_LEN_MASK;
  tmp |= (td_size & TRB_TD_SIZE_MASK) << TRB_TD_SIZE_SHIFT;
  p_trb->dw2 = tmp;

  // bit0: mark the enqueue pointer of the transfer ring
  // bit2: flag for short packet
  // bit5: interrupt on complete
  // bit7: append zero length packet
  // bit[10:15]: TRB type
  // bit16: indicates the direction of data transfer
  // bit[17:18]: setup tag
  tmp = pcs & TRB_CYCLE_BIT_MASK;
  tmp |= 0x1 << TRB_INTR_ON_SHORT_PKT_SHIFT;
  tmp |= (IOC & TRB_INTR_ON_COMPLETION_MASK) << TRB_INTR_ON_COMPLETION_SHIFT;
  tmp |= (TRB_TYPE_XFER_DATA_STAGE & TRB_TYPE_MASK) << TRB_TYPE_SHIFT;
  tmp |= (AZP & TRB_APPEND_ZLP_MASK) << TRB_APPEND_ZLP_SHIFT;
  tmp |= (dir & DATA_STAGE_TRB_DIR_MASK) << DATA_STAGE_TRB_DIR_SHIFT;
  tmp |= (setup_tag & TRB_SETUP_TAG_MASK) << TRB_SETUP_TAG_SHIFT;
  p_trb->dw3 = tmp;

  // Make sure the TRB was built before starting the DMA transfer
  hw_wmb();
}

// Queue Control TRBs
void CrgUdc::UdcQueueCtrl(Endpoint* ep, uint32_t need_trbs_num) {
  auto* enq_pt = ep->enq_pt;
  auto* dq_pt = ep->deq_pt;
  TRBlock* p_trb;
  uint32_t transfer_length;
  uint32_t td_size = 0;
  uint8_t IOC;
  uint8_t AZP;
  uint8_t dir = 0;

  if (ep->ep_state != EpState::kEpStateRunning) {
    zxlogf(ERROR, "UdcQueueCtrl: EP status = %d", ep->ep_state);
    return;
  }

  if (enq_pt == dq_pt) {
    uint32_t tmp = 0;
    bool need_zlp = false;

    dir = (setup_state_ == SetupState::kDataStageXfer ? 0 : 1);
    if (ep->zlp && (ep->req_length != 0) && (ep->req_length % ep->max_packet_size == 0)) {
      need_zlp = true;
    }

    for (uint32_t i = 0; i < need_trbs_num; i++) {
      p_trb = enq_pt;
      if (i < (need_trbs_num - 1)) {
        transfer_length = TRB_MAX_BUFFER_SIZE;
        IOC = 0;
        AZP = 0;
      } else {
        tmp = TRB_MAX_BUFFER_SIZE * i;
        transfer_length = ep->req_length - tmp;
        IOC = 1;
        AZP = (need_zlp ? 1 : 0);
      }
      SetupDataStageTrb(ep, p_trb, ep->pcs, transfer_length, td_size, IOC, AZP, dir, setup_tag_);
      enq_pt++;

      if (((enq_pt->dw3 >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) == TRB_TYPE_LINK) {
        if ((enq_pt->dw3 >> TRB_LINK_TOGGLE_CYCLE_SHIFT) & TRB_LINK_TOGGLE_CYCLE_MASK) {
          enq_pt->dw3 &= ~(TRB_CYCLE_BIT_MASK << TRB_CYCLE_BIT_SHIFT);
          enq_pt->dw3 |= (ep->pcs & TRB_CYCLE_BIT_MASK) << TRB_CYCLE_BIT_SHIFT;
          ep->pcs ^= 0x1;
          // Make sure the PCS was updated before resetting the enqueue pointer
          hw_wmb();
        }
        enq_pt = ep->first_trb;
      }
    }

    ep->enq_pt = enq_pt;
    KnockDoorbell(ep->ep_num);
  } else {
    zxlogf(ERROR, "Eq = 0x%p != Dq = 0x%p", enq_pt, dq_pt);
  }
}

// Queue Transfer TRBs
void CrgUdc::UdcQueueTrbs(Endpoint* ep, uint32_t xfer_ring_size, uint32_t need_trbs_num,
                          uint32_t buffer_length) {
  bool need_zlp = false;
  bool full_td = true;
  bool all_trbs_queued = false;
  uint32_t free_trbs_num = 0;
  uint32_t count;
  uint32_t buffer_length_tmp = 0;
  uint8_t short_pkt = 0;
  uint8_t td_size;
  uint8_t chain_bit = 1;
  uint8_t intr_on_compl = 0;
  uint32_t intr_rate;
  uint32_t j = 1;
  uint64_t req_buf = static_cast<uint64_t>(ep->phys) + ep->req_offset;
  auto* enq_pt = ep->enq_pt;

  if (ep->zlp && (ep->req_length != 0) && (ep->req_length % ep->max_packet_size == 0)) {
    need_zlp = true;
  }

  td_size = static_cast<uint8_t>(need_trbs_num);
  free_trbs_num = RoomOnRing(xfer_ring_size, ep->first_trb, ep->enq_pt, ep->deq_pt);

  if (ep->trbs_needed) {
    req_buf += ep->req_length - ep->req_length_left;
  }

  if (free_trbs_num > need_trbs_num) {
    count = need_trbs_num;
  } else {
    count = free_trbs_num;
    full_td = false;
    ep->transfer_ring_full = true;
    need_zlp = false;
  }

  for (uint32_t i = 0; i < count; i++) {
    buffer_length_tmp = (buffer_length > TRB_MAX_BUFFER_SIZE) ? TRB_MAX_BUFFER_SIZE : buffer_length;
    buffer_length -= buffer_length_tmp;

    if (ep->dir_out) {
      short_pkt = 1;
    }
    if (buffer_length == 0) {
      chain_bit = 0;
      intr_on_compl = 1;
      all_trbs_queued = true;
    }
    intr_rate = 5;
    if (!full_td && j == intr_rate) {
      intr_on_compl = 1;
      j = 0;
    }

    uint8_t pcs = ep->pcs;
    uint8_t AZP = 0;
    if (all_trbs_queued) {
      AZP = (need_zlp ? 1 : 0);
    }
    SetupNormalTrb(enq_pt, buffer_length_tmp, req_buf, td_size - 1, pcs, TRB_TYPE_XFER_NORMAL,
                   short_pkt, chain_bit, intr_on_compl, 0, 0, 0, 0, 0, AZP);
    req_buf += buffer_length_tmp;
    td_size--;
    enq_pt++;
    j++;
    if (((enq_pt->dw3 >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) == TRB_TYPE_LINK) {
      if ((enq_pt->dw3 >> TRB_LINK_TOGGLE_CYCLE_SHIFT) & TRB_LINK_TOGGLE_CYCLE_MASK) {
        enq_pt->dw3 &= ~(TRB_CYCLE_BIT_MASK << TRB_CYCLE_BIT_SHIFT);
        enq_pt->dw3 |= (ep->pcs & TRB_CYCLE_BIT_MASK) << TRB_CYCLE_BIT_SHIFT;
        ep->pcs ^= 0x1;
        // Make sure the PCS was updated before resetting the enqueue pointer
        hw_wmb();
        enq_pt = ep->first_trb;
      }
    }
  }
  ep->enq_pt = enq_pt;
  ep->req_length_left = buffer_length;
  ep->trbs_needed = td_size;
}

// Trigger the doorbell register to start DMA
void CrgUdc::KnockDoorbell(uint8_t ep_num) {
  auto* mmio = get_mmio();

  // Make sure all operation was finished bebore start the DMA transfer
  hw_wmb();
  auto tmp = ep_num & 0x1f;
  DOORBELL::Get().ReadFrom(mmio).set_db_target(tmp).WriteTo(mmio);
}

// Build the transfer TD
void CrgUdc::BuildTransferTd(Endpoint* ep) {
  uint32_t num_trbs_needed;
  uint32_t ring_size = 0;
  uint32_t buffer_length;

  if (ep->trbs_needed) {
    // pending data of the previous request
    buffer_length = ep->req_length_left;
    num_trbs_needed = ep->trbs_needed;
  } else {
    buffer_length = ep->req_length;
    num_trbs_needed = buffer_length / TRB_MAX_BUFFER_SIZE;
    if (buffer_length == 0 || (buffer_length % TRB_MAX_BUFFER_SIZE)) {
      num_trbs_needed += 1;
    }
  }

  if (ep->ep_num == 0) {
    UdcQueueCtrl(ep, num_trbs_needed);
  } else if (ep->type == USB_ENDPOINT_BULK || ep->type == USB_ENDPOINT_INTERRUPT) {
    if (ep->type == USB_ENDPOINT_BULK) {
      ring_size = CRGUDC_BULK_EP_TD_RING_SIZE;
    } else if (ep->type == USB_ENDPOINT_INTERRUPT) {
      ring_size = CRGUDC_INT_EP_TD_RING_SIZE;
    }

    UdcQueueTrbs(ep, ring_size, num_trbs_needed, buffer_length);
    KnockDoorbell(ep->ep_num);
  }
}

// Start to transfer data
void CrgUdc::StartTransfer(Endpoint* ep, uint32_t length) {
  auto ep_num = ep->ep_num;
  bool is_in = ep->dir_in;

  if (length > 0) {
    if (is_in) {
      if (ep_num == 0) {
        ep0_buffer_.CacheFlush(ep->req_offset, length);
      } else {
        usb_request_cache_flush(ep->current_req, ep->req_offset, length);
      }
    } else {
      if (ep_num == 0) {
        ep0_buffer_.CacheFlushInvalidate(ep->req_offset, length);
      } else {
        usb_request_cache_flush_invalidate(ep->current_req, ep->req_offset, length);
      }
    }
  }

  // Construct transfer TRB and queue to transfer ring
  BuildTransferTd(ep);
}

// Disable the Endpoint
void CrgUdc::DisableEp(uint8_t ep_num) {
  auto* mmio = get_mmio();
  auto* ep = &endpoints_[ep_num];
  EpContext* ep_cx;

  fbl::AutoLock lock(&lock_);

  if (ep->ep_state == EpState::kEpStateDisabled) {
    return;
  }

  EPENABLED::Get().ReadFrom(mmio).set_ep_enabled(0x1 << ep_num).WriteTo(mmio);
  enabled_eps_num_--;

  ep_cx = reinterpret_cast<EpContext*>(endpoint_context_.vaddr) + (ep_num - 2);
  memset(ep_cx, 0, sizeof(struct EpContext));

  if ((enabled_eps_num_ == 0) && (device_state_ == DeviceState::kUsbStateConfigured)) {
    device_state_ = DeviceState::kUsbStateAddress;
  }
  ep->ep_state = EpState::kEpStateDisabled;
}

// Handles transfer complete events for endpoint zero
void CrgUdc::HandleEp0TransferComplete() {
  auto* ep = &endpoints_[0];

  switch (setup_state_) {
    case SetupState::kDataStageXfer:
      setup_state_ = SetupState::kStatusStageRecv;
      BuildEp0Status(ep, 0, 0);
      break;
    case SetupState::kDataStageRecv:
      setup_state_ = SetupState::kStatusStageXfer;
      BuildEp0Status(ep, 0, 0);
      break;
    default:
      SetAddressCallback();
      setup_state_ = SetupState::kWaitForSetup;
      break;
  }
}

// Handles transfer complete events for endpoints other than endpoint zero
void CrgUdc::HandleTransferComplete(uint8_t ep_num) {
  auto* ep = &endpoints_[ep_num];

  ep->lock.Acquire();

  ep->req_offset += ep->req_xfersize;

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

// Clear the pending request
void CrgUdc::CompletePendingRequest(Endpoint* ep) {
  RequestQueue complete_reqs;

  fbl::AutoLock lock(&ep->lock);
  if (ep->current_req) {
    complete_reqs.push(Request(ep->current_req, sizeof(usb_request_t)));
    ep->current_req = nullptr;
  }
  for (auto req = ep->queued_reqs.pop(); req; req = ep->queued_reqs.pop()) {
    complete_reqs.push(std::move(*req));
  }

  ep->enabled = false;

  // Requests must be completed outside of the lock.
  for (auto req = complete_reqs.pop(); req; req = complete_reqs.pop()) {
    req->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
  }
}

// Free the dma buffer
void CrgUdc::DmaBufferFree(BufferInfo* dma_buf) {
  if (dma_buf->vmo_handle != ZX_HANDLE_INVALID) {
    if (dma_buf->pmt_handle != ZX_HANDLE_INVALID) {
      zx_status_t status = zx_pmt_unpin(dma_buf->pmt_handle);
      ZX_DEBUG_ASSERT(status == ZX_OK);
      dma_buf->pmt_handle = ZX_HANDLE_INVALID;
    }

    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)dma_buf->vaddr, dma_buf->len);
    zx_handle_close(dma_buf->vmo_handle);
    dma_buf->vmo_handle = ZX_HANDLE_INVALID;
  }

  dma_buf->vaddr = 0;
  dma_buf->phys = 0;
  dma_buf->len = 0;
}

// Alloc the dma buffer
zx_status_t CrgUdc::DmaBufferAlloc(BufferInfo* dma_buf, uint32_t buf_size) {
  zx_handle_t vmo_handle;

  zx_status_t status = ZX_OK;
  status = zx_vmo_create_contiguous(bti_.get(), buf_size, 0, &vmo_handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to allocate ring buffer vmo: %s", zx_status_get_string(status));
    return status;
  }

  status = zx_vmo_set_cache_policy(vmo_handle, ZX_CACHE_POLICY_UNCACHED);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_vmo_set_cache_policy failed: %s", zx_status_get_string(status));
    zx_handle_close(vmo_handle);
    return status;
  }

  zx_vaddr_t mapped_addr;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo_handle, 0,
                       buf_size, &mapped_addr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_vmar_map failed: %s", zx_status_get_string(status));
    zx_handle_close(vmo_handle);
    return status;
  }

  zx_paddr_t phys = 0;
  zx_handle_t pmt_handle = ZX_HANDLE_INVALID;
  uint32_t options = ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE;
  if (buf_size > zx_system_get_page_size()) {
    options |= ZX_BTI_CONTIGUOUS;
  }
  status = zx_bti_pin(bti_.get(), options, vmo_handle, 0,
                      fbl::round_up(buf_size, zx_system_get_page_size()), &phys, 1, &pmt_handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_bti_pin failed:%s", zx_status_get_string(status));
    zx_vmar_unmap(zx_vmar_root_self(), mapped_addr, buf_size);
    zx_handle_close(vmo_handle);
    return status;
  }

  dma_buf->vmo_handle = vmo_handle;
  dma_buf->pmt_handle = pmt_handle;
  dma_buf->vaddr = (void*)mapped_addr;
  dma_buf->vmo_offset = 0;
  dma_buf->len = buf_size;
  dma_buf->phys = phys;

  return status;
}

// Build the event ring
zx_status_t CrgUdc::InitEventRing() {
  auto* mmio = get_mmio();
  UdcEvent* ring_info = &eventrings_[0];
  uint32_t alloc_len;
  zx_status_t status = ZX_OK;

  // Create Event Ring Segment Table
  if (ring_info->erst.vaddr == nullptr) {
    alloc_len = sizeof(struct ErstData);
    status = DmaBufferAlloc(&(ring_info->erst), alloc_len);
    if (status != ZX_OK) {
      zxlogf(ERROR, "InitEventRing: alloc dma buffer for Event Ring Segment Table:%s",
             zx_status_get_string(status));
      return status;
    }
  }
  ring_info->p_erst = reinterpret_cast<ErstData*>(ring_info->erst.vaddr);

  // Create Event Ring
  if (ring_info->event_ring.vaddr == nullptr) {
    alloc_len = CRG_UDC_EVENT_TRB_NUM * sizeof(struct TRBlock);
    status = DmaBufferAlloc(&(ring_info->event_ring), alloc_len);
    if (status != ZX_OK) {
      zxlogf(ERROR, "InitEventRing: alloc dma buffer for Event Ring:%s",
             zx_status_get_string(status));
      return status;
    }
  }
  ring_info->evt_dq_pt = reinterpret_cast<TRBlock*>(ring_info->event_ring.vaddr);
  ring_info->evt_seg0_last_trb =
      reinterpret_cast<TRBlock*>(ring_info->event_ring.vaddr) + (CRG_UDC_EVENT_TRB_NUM - 1);
  ring_info->CCS = 1;
  ring_info->p_erst->seg_addr_lo = LOWER_32_BITS(static_cast<uint64_t>(ring_info->event_ring.phys));
  ring_info->p_erst->seg_addr_hi = UPPER_32_BITS(static_cast<uint64_t>(ring_info->event_ring.phys));
  ring_info->p_erst->seg_size = htole32(CRG_UDC_EVENT_TRB_NUM);
  ring_info->p_erst->rsvd = 0;
  // Make sure the physical address was allocated before setting the base address
  hw_wmb();

  ERSTSZ::Get().FromValue(0).set_erstsz(1).WriteTo(mmio);
  // Event ring segment table base address
  ERSTBALO::Get()
      .FromValue(0)
      .set_erstba_lo(LOWER_32_BITS(static_cast<uint64_t>(ring_info->erst.phys)))
      .WriteTo(mmio);
  ERSTBAHI::Get()
      .FromValue(0)
      .set_erstba_hi(UPPER_32_BITS(static_cast<uint64_t>(ring_info->erst.phys)))
      .WriteTo(mmio);
  // Event ring dequeue pointer register
  ERDPLO::Get()
      .FromValue(0)
      .set_erdp_lo(LOWER_32_BITS(static_cast<uint64_t>(ring_info->event_ring.phys)) | 0x8)
      .WriteTo(mmio);
  ERDPHI::Get()
      .FromValue(0)
      .set_erdp_hi(UPPER_32_BITS(static_cast<uint64_t>(ring_info->event_ring.phys)))
      .WriteTo(mmio);

  IMAN::Get().ReadFrom(mmio).set_ip(1).set_ie(1).WriteTo(mmio);
  IMOD::Get().ReadFrom(mmio).set_imodi(4000).WriteTo(mmio);

  return status;
}

// Build the device contexts
zx_status_t CrgUdc::InitDeviceContext() {
  auto* mmio = get_mmio();
  zx_status_t status = ZX_OK;
  uint32_t buf_size;

  // ep0 is not included in ep contexts in crg udc
  if (endpoint_context_.vaddr == nullptr) {
    buf_size = (CRG_UDC_MAX_EPS - 2) * sizeof(struct EpContext);
    status = DmaBufferAlloc(&endpoint_context_, buf_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "InitDeviceContext: alloc dma buffer for device context:%s",
             zx_status_get_string(status));
      return status;
    }
  }

  // Device context base address pointer
  DCBAPLO::Get()
      .FromValue(0)
      .set_dcbap_lo(LOWER_32_BITS(static_cast<uint64_t>(endpoint_context_.phys)))
      .WriteTo(mmio);
  DCBAPHI::Get()
      .FromValue(0)
      .set_dcbap_hi(UPPER_32_BITS(static_cast<uint64_t>(endpoint_context_.phys)))
      .WriteTo(mmio);

  return status;
}

// Issue a command
zx_status_t CrgUdc::IssueCmd(enum CmdType type, uint32_t para0, uint32_t para1) {
  auto* mmio = get_mmio();
  zx_status_t status = ZX_OK;
  bool check_complete = false;

  auto value = COMMAND::Get().ReadFrom(mmio).start();
  if (value & 0x1) {
    check_complete = true;
  }

  if (check_complete) {
    value = CMDCTRL::Get().ReadFrom(mmio).cmd_active();
    if (value & 0x1) {
      zxlogf(ERROR, "IssueCmd: previous command is not complete!");
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
  // Make sure the previous command was completed
  hw_wmb();

  CMDPARA0::Get().FromValue(0).set_cmd_para0(para0).WriteTo(mmio);
  CMDPARA1::Get().FromValue(0).set_cmd_para1(para1).WriteTo(mmio);

  CMDCTRL::Get()
      .ReadFrom(mmio)
      .set_cmd_active(1)
      .set_cmd_type(static_cast<uint8_t>(type))
      .WriteTo(mmio);

  if (check_complete) {
    do {
      value = CMDCTRL::Get().ReadFrom(mmio).cmd_active();
    } while (value & 0x1);
    if (CMDCTRL::Get().ReadFrom(mmio).cmd_status()) {
      zxlogf(ERROR, "Command Status: %d", CMDCTRL::Get().ReadFrom(mmio).cmd_status());
      return ZX_ERR_TIMED_OUT;
    }
  }

  return status;
}

// Enable the EP0 port
zx_status_t CrgUdc::InitEp0() {
  zx_status_t status = ZX_OK;
  uint32_t buf_size;

  auto* ep = &endpoints_[0];
  buf_size = CRG_CONTROL_EP_TD_RING_SIZE * sizeof(struct TRBlock);
  if (ep->dma_buf.vaddr == nullptr) {
    status = DmaBufferAlloc(&(ep->dma_buf), buf_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "InitEp0: alloc dma buffer for transfer ring:%s", zx_status_get_string(status));
      return status;
    }
  }

  ep->first_trb = reinterpret_cast<TRBlock*>(ep->dma_buf.vaddr);
  ep->last_trb = ep->first_trb + buf_size - 1;

  ep->enq_pt = ep->first_trb;
  ep->deq_pt = ep->first_trb;
  ep->pcs = 1;
  ep->transfer_ring_full = false;

  // setup link TRB
  ep->last_trb->dw0 = htole32(LOWER_32_BITS(ep->dma_buf.phys));
  ep->last_trb->dw1 = htole32(UPPER_32_BITS(ep->dma_buf.phys));
  ep->last_trb->dw2 = 0;
  // TRB type and Toggle Cycle
  uint32_t dw = (0x1 << TRB_LINK_TOGGLE_CYCLE_SHIFT) | (TRB_TYPE_LINK << TRB_TYPE_SHIFT);
  ep->last_trb->dw3 = htole32(dw);

  auto para0 = (LOWER_32_BITS(ep->dma_buf.phys) & 0xfffffff0) | ep->pcs;
  auto para1 = UPPER_32_BITS(ep->dma_buf.phys);
  status = IssueCmd(CmdType::kCrgCmdIintEp0, para0, para1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitEp0: alloc dma buffer for transfer ring:%s", zx_status_get_string(status));
    return status;
  }

  ep->ep_state = EpState::kEpStateRunning;

  return status;
}

// Enable interrupt and start the device
void CrgUdc::UdcStart() {
  auto* mmio = get_mmio();

  // interrupt related
  CONFIG1::Get()
      .ReadFrom(mmio)
      .set_csc_event_en(1)
      .set_pec_event_en(1)
      .set_ppc_event_en(1)
      .set_prc_event_en(1)
      .set_plc_event_en(1)
      .set_cec_event_en(1)
      .WriteTo(mmio);
  COMMAND::Get().ReadFrom(mmio).set_interrupt_en(1).set_sys_err_en(1).WriteTo(mmio);
  // interrupt related end

  COMMAND::Get().ReadFrom(mmio).set_start(1).WriteTo(mmio);
}

// Check the cable connect status
bool CrgUdc::CableIsConnected() {
  auto* mmio = get_mmio();

  auto val = PORTSC::Get().ReadFrom(mmio).pp();
  if (val) {
    // make sure it is stable
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
    val = PORTSC::Get().ReadFrom(mmio).pp();
    if (val) {
      if (device_state_ < DeviceState::kUsbStatePowered) {
        CONFIG0::Get().ReadFrom(mmio).set_usb3_dis_count_limit(15).WriteTo(mmio);
        zx::nanosleep(zx::deadline_after(zx::msec(3)));
        UdcStart();
        device_state_ = DeviceState::kUsbStatePowered;
      }
      return true;
    }
  }

  return false;
}

// Check whether the event ring is empty
bool CrgUdc::EventRingEmpty() {
  auto* event_ring = &eventrings_[0];

  if (event_ring->evt_dq_pt) {
    auto* event = event_ring->evt_dq_pt;
    if ((event->dw3 & 0x1) != event_ring->CCS) {
      return true;
    }
  }

  return false;
}

// Clear the port PM status
void CrgUdc::ClearPortPM() {
  auto* mmio = get_mmio();

  // USB3 port PM status and control
  U3PORTPMSC::Get()
      .ReadFrom(mmio)
      .set_u1_initiate_en(0)
      .set_u2_initiate_en(0)
      .set_u1_timeout(0)
      .set_u2_timeout(0)
      .WriteTo(mmio);
}

// reset the UDC device
zx_status_t CrgUdc::UdcReset() {
  auto* mmio = get_mmio();

  // reset the controller
  COMMAND::Get().ReadFrom(mmio).set_soft_reset(1).WriteTo(mmio);
  bool done = false;
  for (int i = 0; i < 50; i++) {
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
    if (COMMAND::Get().ReadFrom(mmio).soft_reset() == 0) {
      done = true;
      break;
    }
  }
  if (!done) {
    zxlogf(ERROR, "reset timeout");
    return ZX_ERR_TIMED_OUT;
  }

  ClearPortPM();

  setup_state_ = SetupState::kWaitForSetup;
  device_state_ = DeviceState::kUsbStateAttached;
  dev_addr_ = 0;

  // Complete any pending requests
  for (uint32_t i = 0; i < CRG_UDC_MAX_EPS; i++) {
    auto* ep = &endpoints_[i];
    CompletePendingRequest(ep);
    ep->transfer_ring_full = false;
    ep->ep_state = EpState::kEpStateDisabled;
  }
  enabled_eps_num_ = 0;

  ctrl_req_enq_idx_ = 0;
  memset(ctrl_req_queue_, 0, sizeof(struct SetupPacket) * CTRL_REQ_QUEUE_DEPTH);

  return ZX_OK;
}

// HW related operation
zx_status_t CrgUdc::ResetDataStruct() {
  auto* mmio = get_mmio();
  zx_status_t status = ZX_OK;

  COMMAND::Get().ReadFrom(mmio).set_start(0).set_interrupt_en(0).WriteTo(mmio);
  // High Speed
  CONFIG0::Get().ReadFrom(mmio).set_max_speed(3).WriteTo(mmio);

  status = InitEventRing();
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitController: init evnet ring:%s", zx_status_get_string(status));
    return status;
  }

  status = InitDeviceContext();
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitController: init device context:%s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

// Reinit the UDC device
void CrgUdc::UdcReInit() {
  auto* mmio = get_mmio();

  setup_state_ = SetupState::kWaitForSetup;
  device_state_ = DeviceState::kUsbStateReconnecting;

  auto ep_enabled = EPENABLED::Get().ReadFrom(mmio).reg_value();
  EPENABLED::Get().FromValue(0).set_reg_value(ep_enabled).WriteTo(mmio);
  for (int i = 0; i < 50; i++) {
    ep_enabled = EPENABLED::Get().ReadFrom(mmio).reg_value();
    if (ep_enabled == 0) {
      break;
    }
  }

  for (uint32_t i = 2; i < CRG_UDC_MAX_EPS; i++) {
    auto* ep = &endpoints_[i];
    ep->enabled = false;
    CompletePendingRequest(ep);
    ep->transfer_ring_full = false;
    ep->ep_state = EpState::kEpStateDisabled;
  }
  enabled_eps_num_ = 0;

  if (dev_addr_ != 0) {
    uint32_t param0 = 0;
    IssueCmd(CmdType::kCrgCmdSetAddr, param0, 0);
    dev_addr_ = 0;
  }
  ClearPortPM();
}

// Update max_packet_size by "Update EP0 config" command
void CrgUdc::UpdateEp0MaxPacketSize() {
  uint16_t maxpacketsize;
  uint32_t param0;

  if (device_speed_ >= USB_SPEED_SUPER) {
    maxpacketsize = 512;
  } else {
    maxpacketsize = 64;
  }
  param0 = maxpacketsize << 16;
  IssueCmd(CmdType::kCrgCmdUpdateEp0Cfg, param0, 0);

  auto* ep = &endpoints_[0];
  ep->max_packet_size = maxpacketsize;
}

void CrgUdc::EnableSetup() {
  auto* mmio = get_mmio();

  CONFIG1::Get().ReadFrom(mmio).set_setup_event_en(1).WriteTo(mmio);
  device_state_ = DeviceState::kUsbStateDefault;
  setup_state_ = SetupState::kWaitForSetup;
}

// Handle port status change event TRB
zx_status_t CrgUdc::HandlePortStatus() {
  auto* mmio = get_mmio();
  uint32_t portsc_val;
  zx_status_t status = ZX_OK;
  uint32_t speed;

  // handle port reset
  portsc_val = PORTSC::Get().ReadFrom(mmio).reg_value();
  PORTSC::Get().FromValue(0).set_reg_value(portsc_val).WriteTo(mmio);

  if (portsc_val & (0x1 << 21)) {
    zx::nanosleep(zx::deadline_after(zx::msec(3)));
    auto portsc = PORTSC::Get().ReadFrom(mmio);
    if (portsc.prc()) {
      zxlogf(INFO, "HandlePortStatus: RPC is still set");
    } else if (portsc.pr()) {
      zxlogf(INFO, "HandlePortStatus: PRC is not set, but PR is set");
    } else {
      if ((portsc.pls() != 0) || !(portsc.reg_value() & 0x2)) {
        return ZX_OK;
      }

      switch (portsc.speed()) {
        case CRG_U3DC_PORTSC_SPEED_SSP:
          speed = USB_SPEED_ENHANCED_SUPER;
          break;
        case CRG_U3DC_PORTSC_SPEED_SS:
          speed = USB_SPEED_SUPER;
          break;
        case CRG_U3DC_PORTSC_SPEED_HS:
          speed = USB_SPEED_HIGH;
          break;
        case CRG_U3DC_PORTSC_SPEED_FS:
          speed = USB_SPEED_FULL;
          break;
        case CRG_U3DC_PORTSC_SPEED_LS:
          speed = USB_SPEED_LOW;
          break;
        default:
          speed = USB_SPEED_UNDEFINED;
          return ZX_OK;
      }

      if (device_state_ > DeviceState::kUsbStateDefault) {
        UdcReInit();
      }

      device_speed_ = speed;
      if (dci_intf_) {
        dci_intf_->SetSpeed(USB_SPEED_HIGH);
      }
      UpdateEp0MaxPacketSize();
      SetConnected(true);

      if (device_state_ < DeviceState::kUsbStateReconnecting) {
        EnableSetup();
      }
    }
  }
  // handle port connection change
  if (portsc_val & (0x1 << 17)) {
    auto portsc = PORTSC::Get().ReadFrom(mmio);
    if (portsc.ccs() && portsc.pp()) {
      zxlogf(INFO, "HandlePortStatus: connect int checked");
      if ((portsc.pls() != 0) || !(portsc.reg_value() & 0x2)) {
        return ZX_OK;
      }

      switch (portsc.speed()) {
        case CRG_U3DC_PORTSC_SPEED_SSP:
          speed = USB_SPEED_ENHANCED_SUPER;
          break;
        case CRG_U3DC_PORTSC_SPEED_SS:
          speed = USB_SPEED_SUPER;
          break;
        case CRG_U3DC_PORTSC_SPEED_HS:
          speed = USB_SPEED_HIGH;
          break;
        case CRG_U3DC_PORTSC_SPEED_FS:
          speed = USB_SPEED_FULL;
          break;
        case CRG_U3DC_PORTSC_SPEED_LS:
        default:
          speed = USB_SPEED_UNDEFINED;
          return ZX_OK;
      }
      device_speed_ = speed;
      if (dci_intf_) {
        dci_intf_->SetSpeed(device_speed_);
      }
      UpdateEp0MaxPacketSize();
      SetConnected(true);

      if (device_state_ < DeviceState::kUsbStateReconnecting) {
        EnableSetup();
      }
    } else if (!portsc.ccs()) {
      bool cable_connected;
      uint32_t ccs_drop_ignore = 0;

      if ((portsc.pls() == 0x0) && (portsc.speed() < CRG_U3DC_PORTSC_SPEED_SS)) {
        ccs_drop_ignore = 1;
        zxlogf(INFO, "HandlePortStatus: ccs glitch detect on HS/FS");
      }

      if (ccs_drop_ignore == 0) {
        device_speed_ = USB_SPEED_UNDEFINED;
      }
      zx::nanosleep(zx::deadline_after(zx::msec(150)));
      cable_connected = CableIsConnected();

      if (cable_connected && (ccs_drop_ignore == 0)) {
        device_state_ = DeviceState::kUsbStatePowered;
        UdcReInit();
        SetConnected(false);
      } else if (!cable_connected) {
        zxlogf(INFO, "HandlePortStatus: cable disconnected, rst controller");

        UdcReset();
        ResetDataStruct();
        SetConnected(false);
        InitEp0();
        device_state_ = DeviceState::kUsbStateAttached;
        return ZX_ERR_INTERNAL;
      }
    }
  }

  if (portsc_val & (0x1 << 22)) {
    auto portsc = PORTSC::Get().ReadFrom(mmio);
    if (portsc.pls() == 0xf) {
      PORTSC::Get().FromValue(0).set_pls(0).WriteTo(mmio);
    } else if (portsc.pls() == 0x3) {
      // The USB cable is unplugged
      SetConnected(false);
      for (uint8_t i = 0; i < std::size(endpoints_); i++) {
        auto* ep = &endpoints_[i];
        DmaBufferFree(&ep->dma_buf);
      }
      auto* event_ring = &eventrings_[0];
      DmaBufferFree(&event_ring->erst);
      DmaBufferFree(&event_ring->event_ring);
      DmaBufferFree(&endpoint_context_);

      UdcReset();
      ResetDataStruct();
      InitEp0();
      device_speed_ = USB_SPEED_UNDEFINED;
      CableIsConnected();
    }
  }

  return status;
}

zx_paddr_t CrgUdc::TranTrbDmaToVirt(Endpoint* ep, zx_paddr_t phy) {
  zx_paddr_t offset;

  offset = phy - ep->dma_buf.phys;
  offset = offset / sizeof(struct TRBlock);

  return offset;
}

zx_paddr_t CrgUdc::EventTrbVirtToDma(UdcEvent* event_ring, TRBlock* event) {
  zx_paddr_t offset;
  uint32_t trb_idx;

  trb_idx = static_cast<uint32_t>(event - reinterpret_cast<TRBlock*>(event_ring->event_ring.vaddr));
  offset = trb_idx * sizeof(struct TRBlock);
  return event_ring->event_ring.phys + offset;
}

// Issue command "Initialize EP0" to reset EP0 logic and initialize its transfer ring
zx_status_t CrgUdc::PrepareForSetup() {
  uint32_t param0;
  uint32_t param1;

  if (!EventRingEmpty() || portsc_on_reconnecting_ == 1) {
    zxlogf(ERROR, "not ready for setup");
    return ZX_ERR_SHOULD_WAIT;
  }

  auto* ep = &endpoints_[0];
  CompletePendingRequest(ep);

  ctrl_req_enq_idx_ = 0;
  memset(ctrl_req_queue_, 0, sizeof(struct SetupPacket) * CTRL_REQ_QUEUE_DEPTH);

  param0 = (LOWER_32_BITS(ep->dma_buf.phys) & 0xfffffff0) | ep->pcs;
  param1 = UPPER_32_BITS(ep->dma_buf.phys);
  IssueCmd(CmdType::kCrgCmdIintEp0, param0, param1);

  ep->deq_pt = ep->enq_pt;
  ep->transfer_ring_full = false;

  EnableSetup();

  return ZX_OK;
}

void CrgUdc::QueueSetupPkt(usb_setup_t* setup_pkt, uint16_t setup_tag) {
  if (ctrl_req_enq_idx_ == CTRL_REQ_QUEUE_DEPTH) {
    return;
  }
  memcpy(&(ctrl_req_queue_[ctrl_req_enq_idx_].usbctrlreq), setup_pkt, sizeof(struct usb_setup));
  ctrl_req_queue_[ctrl_req_enq_idx_].setup_tag = setup_tag;

  ctrl_req_enq_idx_++;
}

// Handle the event TRB
zx_status_t CrgUdc::UdcHandleEvent(TRBlock* event) {
  zx_status_t status = ZX_OK;

  // trb type
  switch ((event->dw3 >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK) {
    case TRB_TYPE_EVT_PORT_STATUS_CHANGE:
      if (device_state_ == DeviceState::kUsbStateReconnecting) {
        portsc_on_reconnecting_ = 1;
        break;
      }
      status = HandlePortStatus();
      break;
    case TRB_TYPE_EVT_TRANSFER:
      if (device_state_ < DeviceState::kUsbStateReconnecting) {
        zxlogf(ERROR, "UdcHandleEvent: Xfer compl event rcved when kUsbStateReconnecting");
        break;
      }
      status = HandleXferEvent(event);
      break;
    case TRB_TYPE_EVT_SETUP_PKT:
      usb_setup_t* setup_pkt;
      uint16_t setup_tag;

      setup_pkt = reinterpret_cast<usb_setup_t*>(&event->dw0);
      setup_tag = (event->dw3 >> EVE_TRB_SETUP_TAG_SHIFT) & EVE_TRB_SETUP_TAG_MASK;
      if (setup_state_ != SetupState::kWaitForSetup) {
        QueueSetupPkt(setup_pkt, setup_tag);
        break;
      }

      memcpy(&cur_setup_, setup_pkt, sizeof(cur_setup_));
      setup_tag_ = setup_tag;
      HandleEp0Setup();
      break;
    default:
      zxlogf(ERROR, "UdcHandleEvent: unexpect TRB_TYPE");
      break;
  }

  return status;
}

// Process the event ring
zx_status_t CrgUdc::ProcessEventRing() {
  auto* mmio = get_mmio();
  zx_status_t status = ZX_OK;

  IMAN::Get().ReadFrom(mmio).set_ip(1).WriteTo(mmio);
  auto* event_ring = &eventrings_[0];
  while (event_ring->evt_dq_pt) {
    hw_rmb();
    auto* event = event_ring->evt_dq_pt;

    if ((event->dw3 & EVE_TRB_CYCLE_BIT_MASK) != event_ring->CCS) {
      break;
    }
    status = UdcHandleEvent(event);
    if (status != ZX_OK) {
      zxlogf(ERROR, "ProcessEventRing: handle event:%s", zx_status_get_string(status));
      return status;
    }

    if (event == event_ring->evt_seg0_last_trb) {
      event_ring->CCS = event_ring->CCS ? 0 : 1;
      event_ring->evt_dq_pt = reinterpret_cast<TRBlock*>(event_ring->event_ring.vaddr);
    } else {
      event_ring->evt_dq_pt++;
    }
  }

  // update dequeue pointer
  zx_paddr_t erdp = EventTrbVirtToDma(event_ring, event_ring->evt_dq_pt);
  ERDPHI::Get().ReadFrom(mmio).set_erdp_hi(UPPER_32_BITS(erdp)).WriteTo(mmio);
  ERDPLO::Get().ReadFrom(mmio).set_erdp_lo(LOWER_32_BITS(erdp) | (0x1 << 3)).WriteTo(mmio);

  return status;
}

// Fill the device context for EPs
void CrgUdc::EpContextSetup(const usb_endpoint_descriptor_t* ep_desc,
                            const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
  uint16_t maxburst = 0;
  uint8_t maxstreams = 0;
  uint32_t dw = 0;
  uint32_t ep_type;

  uint8_t ep_num = CRG_UDC_ADDR_TO_INDEX(ep_desc->b_endpoint_address);
  bool is_in = (ep_desc->b_endpoint_address & USB_DIR_MASK) == USB_DIR_IN;

  auto* ep = &endpoints_[ep_num];
  ep_type = usb_ep_type(ep_desc);

  uint16_t max_packet_size = usb_ep_max_packet(ep_desc);
  if (device_speed_ >= USB_SPEED_SUPER) {
    maxburst = ss_comp_desc->b_max_burst;
    if (ep_type == USB_ENDPOINT_BULK) {
      maxstreams = ss_comp_desc->bm_attributes & 0x1f;
    }
  } else if ((device_speed_ == USB_SPEED_HIGH || device_speed_ == USB_SPEED_FULL) &&
             (ep_type == USB_ENDPOINT_INTERRUPT)) {
    if (device_speed_ == USB_SPEED_HIGH) {
      maxburst = usb_ep_add_mf_transactions(ep_desc);
    }
    maxburst = (maxburst == 0x3) ? 0x2 : maxburst;
  }

  // corigine gadget dir should be opposite to host dir
  if (!is_in) {
    ep_type = usb_ep_type(ep_desc) + EP_TYPE_INVALID2;
  }

  if (maxstreams) {
    zxlogf(INFO, " maxstream=%d is not expected", maxstreams);
  }
  // fill endpoint context
  auto* epcx = reinterpret_cast<EpContext*>(endpoint_context_.vaddr) + (ep_num - 2);
  // dw0: logical EP number - bit[3:6], Interval - bit[16:23]
  dw = ((ep_num / 2) & EP_CX_LOGICAL_EP_NUM_MASK) << EP_CX_LOGICAL_EP_NUM_SHIFT;
  dw |= (ep_desc->b_interval & EP_CX_INTERVAL_MASK) << EP_CX_INTERVAL_SHIFT;
  epcx->dw0 = htole32(dw);

  // dw1: EP Type - bit[3:5], Max Burst Size - bit[8:15], Max Packet Size - bit[16:31]
  dw = (static_cast<uint32_t>(ep_type) & EP_CX_EP_TYPE_MASK) << EP_CX_EP_TYPE_SHIFT;
  dw |= (maxburst & EP_CX_MAX_BURST_SIZE_MASK) << EP_CX_MAX_BURST_SIZE_SHIFT;
  dw |= (max_packet_size & EP_CX_MAX_PACKET_SIZE_MASK) << EP_CX_MAX_PACKET_SIZE_SHIFT;
  epcx->dw1 = htole32(dw);

  // dw2: DCS - bit0, TR Dequeue Pointer Lo - [4:31]
  dw = ep->pcs & EP_CX_DEQ_CYC_STATE_MASK;
  dw |= LOWER_32_BITS(static_cast<uint64_t>(ep->dma_buf.phys)) & EP_CX_TR_DQPT_LO_MASK;
  epcx->dw2 = htole32(dw);

  // dw3: TR Dequeue Pointer Hi - [0:31]
  dw = UPPER_32_BITS(static_cast<uint64_t>(ep->dma_buf.phys));
  epcx->dw3 = htole32(dw);

  // Make sure the device context was build before starting the configuration command
  hw_wmb();
}

zx_status_t CrgUdc::InitController() {
  auto* mmio = get_mmio();
  uint32_t reg_val;
  zx_status_t status = ZX_OK;

  // set controller to device role
  reg_val = mmio->Read32(0x20fc);
  reg_val |= 0x1;
  mmio->Write32(reg_val, 0x20fc);

  status = UdcReset();
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitController: reset udc controller:%s", zx_status_get_string(status));
    return status;
  }

  ClearPortPM();

  status = ResetDataStruct();
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitController: reset data struct:%s", zx_status_get_string(status));
    return status;
  }

  status = InitEp0();
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitController: init EP0:%s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void CrgUdc::SetConnected(bool connected) {
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

zx_status_t CrgUdc::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<CrgUdc>(parent);
  auto status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t CrgUdc::Init() {
  pdev_ = ddk::PDev::FromFragment(parent());
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "CrgUdc::Create: could not get platform device protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // USB PHY protocol is optional.
  usb_phy_ = ddk::UsbPhyProtocolClient(parent(), "udc-phy");
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
    zxlogf(ERROR, "CrgUdc::Init can't get driver metadata");
    return ZX_ERR_INTERNAL;
  }

  status = pdev_.MapMmio(0, &mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CrgUdc::Init MapMmio failed: %s", zx_status_get_string(status));
    return status;
  }

  status = pdev_.GetInterrupt(0, &irq_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CrgUdc::Init GetInterrupt failed: %s", zx_status_get_string(status));
    return status;
  }

  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CrgUdc::Init GetBti failed: %s", zx_status_get_string(status));
    return status;
  }

  status = ep0_buffer_.Init(bti_.get(), UINT16_MAX, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CrgUdc::Init ep0_buffer_.Init failed: %s", zx_status_get_string(status));
    return status;
  }

  status = ep0_buffer_.PhysMap();
  if (status != ZX_OK) {
    zxlogf(ERROR, "CrgUdc::Init ep0_buffer_.PhysMap failed: %s", zx_status_get_string(status));
    return status;
  }

  if ((status = InitController()) != ZX_OK) {
    zxlogf(ERROR, "CrgUdc::Init InitController failed: %s", zx_status_get_string(status));
    return status;
  }

  status = DdkAdd("udc");
  if (status != ZX_OK) {
    zxlogf(ERROR, "CrgUdc::Init DdkAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void CrgUdc::DdkInit(ddk::InitTxn txn) {
  int rc = thrd_create_with_name(
      &irq_thread_, [](void* arg) -> int { return reinterpret_cast<CrgUdc*>(arg)->IrqThread(); },
      reinterpret_cast<void*>(this), "udc-interrupt-thread");
  if (rc == thrd_success) {
    irq_thread_started_ = true;
    txn.Reply(ZX_OK);
  } else {
    txn.Reply(ZX_ERR_INTERNAL);
  }
}

int CrgUdc::IrqThread() {
  auto* mmio = get_mmio();
  const zx::duration capacity = zx::usec(125);
  const zx::duration deadline = zx::msec(1);
  const zx::duration period = deadline;
  zx::profile profile;
  zx_status_t status = device_get_deadline_profile(parent_, capacity.get(), deadline.get(),
                                                   period.get(), "src/devices/usb/drivers/crg-udc",
                                                   profile.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(WARNING, "%s Failed to get deadline profile: %s", __FUNCTION__,
           zx_status_get_string(status));
  } else {
    status = zx_object_set_profile(thrd_get_zx_handle(thrd_current()), profile.get(), 0);
    if (status != ZX_OK) {
      // This should be an error since we won't be able to guarantee we can meet deadlines.
      // Failure to meet deadlines can result in undefined behavior on the bus.
      zxlogf(ERROR, "%s: Failed to apply deadline profile to IRQ thread: %s", __FUNCTION__,
             zx_status_get_string(status));
    }
  }

  if (!CableIsConnected()) {
    zxlogf(INFO, "crg_udc: the cable is not connected");
    return 0;
  }

  while (1) {
    wait_start_time_ = zx::clock::get_monotonic();
    auto wait_res = irq_.wait(&irq_timestamp_);
    irq_dispatch_timestamp_ = zx::clock::get_monotonic();
    if (wait_res == ZX_ERR_CANCELED) {
      break;
    } else if (wait_res != ZX_OK) {
      zxlogf(ERROR, "crg_udc: irq wait failed, retcode = %s", zx_status_get_string(wait_res));
    }

    // It doesn't seem that this inner loop should be necessary,
    // but without it we miss interrupts on some versions of the IP.
    while (1) {
      auto usbstatus = STATUS::Get().ReadFrom(mmio);

      if (usbstatus.sys_err() == 1) {
        zxlogf(ERROR, "crg_udc: system error");
        STATUS::Get().FromValue(0).set_sys_err(1).WriteTo(mmio);
        break;
      }

      if (usbstatus.eint() == 1) {
        STATUS::Get().FromValue(0).set_eint(1).WriteTo(mmio);
        // process event ring
        ProcessEventRing();
      }

      if (device_state_ == DeviceState::kUsbStateReconnecting && portsc_on_reconnecting_ == 1 &&
          EventRingEmpty()) {
        portsc_on_reconnecting_ = 0;
        HandlePortStatus();
      }

      if (device_state_ == DeviceState::kUsbStateReconnecting && connected_) {
        PrepareForSetup();
      }
    }
  }

  zxlogf(INFO, "crg_udc: irq thread finished");
  return 0;
}

void CrgUdc::DdkUnbind(ddk::UnbindTxn txn) {
  irq_.destroy();
  if (irq_thread_started_) {
    irq_thread_started_ = false;
    thrd_join(irq_thread_, nullptr);
  }
  txn.Reply();
}

void CrgUdc::DdkRelease() { delete this; }

void CrgUdc::DdkSuspend(ddk::SuspendTxn txn) {
  fbl::AutoLock lock(&lock_);
  irq_.destroy();
  shutting_down_ = true;
  lock.release();

  if (irq_thread_started_) {
    irq_thread_started_ = false;
    thrd_join(irq_thread_, nullptr);
  }

  // transfer ring
  for (uint8_t i = 0; i < std::size(endpoints_); i++) {
    auto* ep = &endpoints_[i];
    DmaBufferFree(&ep->dma_buf);
  }

  // event ring
  auto* event_ring = &eventrings_[0];
  DmaBufferFree(&event_ring->erst);
  DmaBufferFree(&event_ring->event_ring);

  // device contexts
  DmaBufferFree(&endpoint_context_);

  ep0_buffer_.release();
  txn.Reply(ZX_OK, 0);
}

void CrgUdc::UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_callback_t* cb) {
  {
    fbl::AutoLock lock(&lock_);
    if (shutting_down_) {
      lock.release();
      usb_request_complete(req, ZX_ERR_IO_NOT_PRESENT, 0, cb);
    }
  }
  uint8_t ep_num = CRG_UDC_ADDR_TO_INDEX(req->header.ep_address);
  if (ep_num == 0 || ep_num >= std::size(endpoints_)) {
    zxlogf(ERROR, "CrgUdc::UsbDciRequestQueue: bad ep address 0x%02X", req->header.ep_address);
    usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
    return;
  }
  zxlogf(SERIAL, "UsbDciRequestQueue ep %u length %zu", ep_num, req->header.length);

  auto* ep = &endpoints_[ep_num];

  if (!ep->enabled) {
    usb_request_complete(req, ZX_ERR_BAD_STATE, 0, cb);
    zxlogf(ERROR, "the endpoint %d not enabled", ep_num);
    return;
  }

  // OUT transactions must have length > 0 and multiple of max packet size
  if (CRG_UDC_EP_IS_OUT(ep_num)) {
    if (req->header.length == 0 || req->header.length % ep->max_packet_size != 0) {
      zxlogf(ERROR, "udc_ep_queue: OUT transfers must be multiple of max packet size");
      usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
      return;
    }
  }

  fbl::AutoLock lock(&ep->lock);

  if (!configured_) {
    zxlogf(ERROR, "udc_ep_queue not configured!");
    usb_request_complete(req, ZX_ERR_BAD_STATE, 0, cb);
    return;
  }

  ep->queued_reqs.push(Request(req, *cb, sizeof(usb_request_t)));
  QueueNextRequest(ep);
}

zx_status_t CrgUdc::UsbDciSetInterface(const usb_dci_interface_protocol_t* interface) {
  if (dci_intf_) {
    zxlogf(ERROR, "dci_intf_ already set");
    return ZX_ERR_BAD_STATE;
  }

  dci_intf_ = ddk::UsbDciInterfaceProtocolClient(interface);

  return ZX_OK;
}

zx_status_t CrgUdc::UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
  uint32_t param0;
  zx_status_t status = ZX_OK;

  uint8_t ep_num = CRG_UDC_ADDR_TO_INDEX(ep_desc->b_endpoint_address);
  if (ep_num == 0 || ep_num == 1 || ep_num >= std::size(endpoints_)) {
    zxlogf(ERROR, "CrgUdc::UsbDciConfigEp: bad ep address 0x%02X", ep_desc->b_endpoint_address);
    return ZX_ERR_INVALID_ARGS;
  }

  bool is_in = (ep_desc->b_endpoint_address & USB_DIR_MASK) == USB_DIR_IN;
  uint8_t ep_type = usb_ep_type(ep_desc);
  uint16_t max_packet_size = usb_ep_max_packet(ep_desc);

  if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
    zxlogf(ERROR, "CrgUdc::UsbDciConfigEp: isochronous endpoints are not supported");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto* ep = &endpoints_[ep_num];
  fbl::AutoLock lock(&ep->lock);

  ep->type = ep_type;
  ep->max_packet_size = max_packet_size;
  if (is_in) {
    ep->dir_in = true;
    ep->dir_out = false;
  } else {
    ep->dir_in = false;
    ep->dir_out = true;
  }

  if (ep->ep_state != EpState::kEpStateDisabled) {
    DisableEp(ep_num);
  }

  if (ep->dma_buf.vaddr == nullptr) {
    uint32_t ring_size = 0;
    uint32_t alloc_len;
    if (ep_type == USB_ENDPOINT_BULK) {
      ring_size = CRGUDC_BULK_EP_TD_RING_SIZE;
    } else if (ep_type == USB_ENDPOINT_INTERRUPT) {
      ring_size = CRGUDC_INT_EP_TD_RING_SIZE;
    }
    alloc_len = ring_size * sizeof(struct TRBlock);
    status = DmaBufferAlloc(&(ep->dma_buf), alloc_len);
    if (status != ZX_OK) {
      zxlogf(ERROR, "UsbDciConfigEp: alloc dma buffer for transfer ring:%s",
             zx_status_get_string(status));
      return status;
    }
    ep->first_trb = reinterpret_cast<TRBlock*>(ep->dma_buf.vaddr);
    ep->last_trb = ep->first_trb + ring_size - 1;

    // setup link trb
    ep->last_trb->dw0 = LOWER_32_BITS(static_cast<uint64_t>(ep->dma_buf.phys));
    ep->last_trb->dw1 = UPPER_32_BITS(static_cast<uint64_t>(ep->dma_buf.phys));
    ep->last_trb->dw2 = 0;
    uint32_t dw = (0x1 << TRB_LINK_TOGGLE_CYCLE_SHIFT) | (TRB_TYPE_LINK << TRB_TYPE_SHIFT);
    ep->last_trb->dw3 = htole32(dw);
    // Make sure the link TRB was build before setting enqueue/dequeue pointer
    hw_wmb();

    ep->enq_pt = ep->first_trb;
    ep->deq_pt = ep->first_trb;
    ep->pcs = 1;
    ep->transfer_ring_full = false;
    enabled_eps_num_++;
    EpContextSetup(ep_desc, ss_comp_desc);
  }

  param0 = 0x1 << ep_num;
  IssueCmd(CmdType::kCrgCmdConfigEp, param0, 0);

  ep->enabled = true;
  ep->ep_state = EpState::kEpStateRunning;
  if (device_state_ == DeviceState::kUsbStateAddress) {
    device_state_ = DeviceState::kUsbStateConfigured;
  }

  if (configured_) {
    QueueNextRequest(ep);
  }

  return ZX_OK;
}

zx_status_t CrgUdc::UsbDciDisableEp(uint8_t ep_address) {
  uint8_t ep_num = CRG_UDC_ADDR_TO_INDEX(ep_address);
  if (ep_num == 0 || ep_num == 1 || ep_num >= std::size(endpoints_)) {
    zxlogf(ERROR, "CrgUdc::UsbDciConfigEp: bad ep address 0x%02X", ep_address);
    return ZX_ERR_INVALID_ARGS;
  }

  auto* ep = &endpoints_[ep_num];

  fbl::AutoLock lock(&ep->lock);

  DisableEp(ep_num);
  ep->enabled = false;

  return ZX_OK;
}

zx_status_t CrgUdc::UsbDciEpSetStall(uint8_t ep_address) {
  // TODO(voydanoff) implement this
  return ZX_OK;
}

zx_status_t CrgUdc::UsbDciEpClearStall(uint8_t ep_address) {
  // TODO(voydanoff) implement this
  return ZX_OK;
}

size_t CrgUdc::UsbDciGetRequestSize() { return Request::RequestSize(sizeof(usb_request_t)); }

zx_status_t CrgUdc::UsbDciCancelAll(uint8_t epid) {
  uint8_t ep_num = CRG_UDC_ADDR_TO_INDEX(epid);
  auto* ep = &endpoints_[ep_num];

  fbl::AutoLock lock(&ep->lock);
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
  ops.bind = CrgUdc::Create;
  return ops;
}();

}  // namespace crg_udc

ZIRCON_DRIVER(crg_udc, crg_udc::driver_ops, "zircon", "0.1");
