// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-event-ring.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/promise.h>
#include <lib/zx/clock.h>

#include "usb-xhci.h"

namespace usb_xhci {

// The minimum required number of event ring segment table entries.
static constexpr uint16_t kMinERSTEntries = 16;

zx_status_t EventRingSegmentTable::Init(size_t page_size, const zx::bti& bti, bool is_32bit,
                                        uint32_t erst_max, ERSTSZ erst_size,
                                        const dma_buffer::BufferFactory& factory,
                                        ddk::MmioBuffer* mmio) {
  erst_size_ = erst_size;
  bti_ = &bti;
  page_size_ = page_size;
  is_32bit_ = is_32bit;
  mmio_.emplace(mmio->View(0));
  zx_status_t status = factory.CreatePaged(bti, page_size_, false, &erst_);
  if (status != ZX_OK) {
    return status;
  }
  if (is_32bit && (erst_->phys()[0] >= UINT32_MAX)) {
    return ZX_ERR_NO_MEMORY;
  }

  count_ = page_size / sizeof(ERSTEntry);
  if (count_ > erst_max) {
    count_ = erst_max;
  }
  entries_ = static_cast<ERSTEntry*>(erst_->virt());
  return ZX_OK;
}

zx_status_t EventRingSegmentTable::AddSegment(zx_paddr_t paddr) {
  if (offset_ >= count_) {
    if (offset_ > count_) {
      return ZX_ERR_BAD_STATE;
    }
    return ZX_ERR_NO_MEMORY;
  }
  ERSTEntry entry;
  entry.address_low = static_cast<uint32_t>(paddr & UINT32_MAX);
  entry.address_high = static_cast<uint32_t>(paddr >> 32);
  entry.size = static_cast<uint16_t>(page_size_ / kMinERSTEntries);
  entries_[offset_] = entry;
  hw_mb();
  offset_++;
  erst_size_.set_TableSize(offset_).WriteTo(&mmio_.value());
  erst_pressure_++;
  return ZX_OK;
}

zx_status_t EventRing::Init(size_t page_size, const zx::bti& bti, ddk::MmioBuffer* buffer,
                            bool is_32bit, uint32_t erst_max, ERSTSZ erst_size, ERDP erdp_reg,
                            IMAN iman_reg, uint8_t cap_length, HCSPARAMS1 hcs_params_1,
                            CommandRing* command_ring, DoorbellOffset doorbell_offset, UsbXhci* hci,
                            HCCPARAMS1 hcc_params_1, uint64_t* dcbaa) {
  fbl::AutoLock l(&segment_mutex_);
  erdp_reg_ = erdp_reg;
  hcs_params_1_ = hcs_params_1;
  mmio_ = buffer;
  bti_ = &bti;
  page_size_ = page_size;
  is_32bit_ = is_32bit;
  mmio_ = buffer;
  iman_reg_ = iman_reg;
  cap_length_ = cap_length;
  command_ring_ = command_ring;
  doorbell_offset_ = doorbell_offset;
  hci_ = hci;
  hcc_params_1_ = hcc_params_1;
  dcbaa_ = dcbaa;
  return segments_.Init(page_size, bti, is_32bit, erst_max, erst_size, hci->buffer_factory(),
                        mmio_);
}

void EventRing::RemovePressure() {
  fbl::AutoLock l(&segment_mutex_);
  segments_.RemovePressure();
}

size_t EventRing::GetPressure() {
  fbl::AutoLock l(&segment_mutex_);
  return segments_.Pressure();
}

zx_status_t EventRing::AddSegmentIfNone() {
  if (!erdp_phys_) {
    return AddSegment();
  }
  return ZX_OK;
}

zx_status_t EventRing::AddTRB() {
  fbl::AutoLock l(&segment_mutex_);
  trbs_++;
  if (trbs_ == segments_.TrbCount()) {
    l.release();
    zx_status_t status = AddSegment();
    if (status != ZX_OK) {
      return status;
    }
    return ZX_OK;
  }
  return ZX_OK;
}

zx_status_t EventRing::AddSegment() {
  fbl::AutoLock l(&segment_mutex_);
  if (segments_.Pressure() < segments_.SegmentCount()) {
    segments_.AddPressure();
    return ZX_OK;
  }
  std::unique_ptr<dma_buffer::ContiguousBuffer> buffer;
  {
    std::unique_ptr<dma_buffer::ContiguousBuffer> buffer_tmp;
    zx_status_t status = hci_->buffer_factory().CreateContiguous(
        *bti_, page_size_,
        static_cast<uint32_t>(page_size_ == zx_system_get_page_size() ? 0 : page_size_ >> 12),
        &buffer_tmp);
    if (status != ZX_OK) {
      return status;
    }
    buffer = std::move(buffer_tmp);
  }
  if (is_32bit_ && (buffer->phys() >= UINT32_MAX)) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = segments_.AddSegment(buffer->phys());
  if (status != ZX_OK) {
    return status;
  }
  bool needs_iterator = false;
  if (!erdp_phys_) {
    erdp_phys_ = buffer->phys();
    erdp_virt_ = static_cast<TRB*>(buffer->virt());
    erdp_ = 0;
    needs_iterator = true;
  }
  buffers_.push_back(std::move(buffer));
  if (needs_iterator) {
    buffers_it_ = buffers_.begin();
  }
  return ZX_OK;
}

TRBPromise EventRing::HandlePortStatusChangeEvent(uint8_t port_id) {
  auto sc = PORTSC::Get(cap_length_, port_id).ReadFrom(mmio_);
  std::optional<TRBPromise> pending_enumeration;
  // Read status bits
  bool needs_enum = false;

  // xHCI doesn't provide a way of retrieving the port speed prior to a device being fully
  // online (without using ACPI or another out-of-band mechanism).
  // In order to correctly enumerate devices, we use heuristics to try and determine
  // whether or not a port is 2.0 or 3.0.
  if (sc.CCS()) {
    // Wait for the port to exit polling state, if applicable.
    // Only 2.0 ports should go into a polling state, so if we get here,
    // we can be sure that it's a 2.0 port. Some controllers may skip this step though....
    if ((sc.PLS() == PORTSC::Polling)) {
      // USB 2.0 port connect
      if (!hci_->get_port_state()[port_id - 1].is_connected) {
        // USB 2.0 requires a port reset to advance to U0
        Usb2DeviceAttach(port_id);
        needs_enum = true;
      }
    } else {
      // USB 3.0 port connect, since we got a connect status bit set,
      // and were not polling.
      if (!hci_->get_port_state()[port_id - 1].is_connected) {
        Usb3DeviceAttach(port_id);
        needs_enum = true;
      }
      if ((sc.PLS() == PORTSC::U0) && (sc.PED()) && (!sc.PR()) &&
          !hci_->get_port_state()[port_id - 1].link_active) {
        // Set the link active bit here to prevent us from onlining the same device twice.
        hci_->get_port_state()[port_id - 1].link_active = true;
        needs_enum = false;
        pending_enumeration = LinkUp(port_id);
      }
    }

    // Link could be active from connect status change above.
    // To prevent enumerating a device twice, we ensure that the link wasn't previously active
    // before enumerating.
    if ((sc.PLS() == PORTSC::U0) && sc.CCS() &&
        !(hci_->get_port_state()[port_id - 1].link_active)) {
      if (!hci_->get_port_state()[port_id - 1].is_connected) {
        // Spontaneous initialization of USB 3.0 port without going through
        // CSC event. We know this is USB 3.0 since this cannot possibly happen
        // with a 2.0 port.
        hci_->get_port_state()[port_id - 1].is_USB3 = true;
        hci_->get_port_state()[port_id - 1].is_connected = true;
      }
      hci_->get_port_state()[port_id - 1].link_active = true;
      if (!hci_->get_port_state()[port_id - 1].is_USB3) {
        // USB 2.0 specification section 9.2.6.3
        // states that we must wait 10 milliseconds.
        needs_enum = false;
        pending_enumeration =
            hci_->Timeout(zx::deadline_after(zx::msec(10)))
                .and_then([=](TRB*& result) { return LinkUp(static_cast<uint8_t>(port_id)); })
                .box();
      } else {
        needs_enum = false;
        pending_enumeration = LinkUp(static_cast<uint8_t>(port_id));
      }
    }

  } else {
    // For hubs, we need to take the device offline from the bus's standpoint before tearing down
    // the hub. This means that the slot has to be kept alive until the hub driver is removed.
    hci_->get_port_state()[port_id - 1].retry = false;
    hci_->get_port_state()[port_id - 1].link_active = false;
    hci_->get_port_state()[port_id - 1].is_connected = false;
    hci_->get_port_state()[port_id - 1].is_USB3 = false;
    if (hci_->get_port_state()[port_id - 1].slot_id) {
      ScheduleTask(hci_->DeviceOffline(hci_->get_port_state()[port_id - 1].slot_id, nullptr).box());
    }
  }

  // Update registers if not init
  if (sc.OCC()) {
    bool overcurrent = sc.OCA();
    PORTSC::Get(cap_length_, port_id)
        .FromValue(0)
        .set_CCS(sc.CCS())
        .set_PortSpeed(sc.PortSpeed())
        .set_PIC(sc.PIC())
        .set_PLS(sc.PLS())
        .set_PP(sc.PP())
        .set_OCC(1)
        .WriteTo(mmio_);
    if (overcurrent) {
      zxlogf(ERROR, "Port %i has overcurrent active.", static_cast<int>(port_id));
    } else {
      zxlogf(ERROR, "Overcurrent event on port %i cleared.", static_cast<int>(port_id));
    }
  }
  if (sc.CSC()) {
    // Connect status change
    hci_->get_port_state()[port_id - 1].retry = false;
    PORTSC::Get(cap_length_, port_id)
        .FromValue(0)
        .set_CCS(sc.CCS())
        .set_PLC(sc.PLC())
        .set_PortSpeed(sc.PortSpeed())
        .set_PIC(sc.PIC())
        .set_PLS(sc.PLS())
        .set_PP(sc.PP())
        .set_CSC(sc.CSC())
        .WriteTo(mmio_);
  }
  if (sc.PEC()) {
    return fpromise::make_error_promise(ZX_ERR_BAD_STATE);
  }
  if (sc.PRC() || sc.WRC()) {
    PORTSC::Get(cap_length_, port_id)
        .FromValue(0)
        .set_CCS(sc.CCS())
        .set_PortSpeed(sc.PortSpeed())
        .set_PIC(sc.PIC())
        .set_PLS(sc.PLS())
        .set_PP(sc.PP())
        .set_PRC(sc.PRC())
        .set_WRC(sc.WRC())
        .WriteTo(mmio_);
  }
  if (pending_enumeration.has_value()) {
    return *std::move(pending_enumeration);
  }
  if (needs_enum) {
    return WaitForPortStatusChange(port_id)
        .and_then([=](TRB*& trb) {
          // Retry enumeration
          HandlePortStatusChangeEventInterrupt(port_id, true);
          return fpromise::ok(trb);
        })
        .box();
  }
  return fpromise::make_ok_promise(static_cast<TRB*>(nullptr));
}

TRBPromise EventRing::WaitForPortStatusChange(uint8_t port_id) {
  fpromise::bridge<TRB*, zx_status_t> bridge;
  auto context = hci_->get_command_ring()->AllocateContext();
  context->completer = std::move(bridge.completer);
  hci_->get_port_state()[port_id - 1].wait_for_port_status_change_ = std::move(context);
  return bridge.consumer.promise();
}

void EventRing::CallPortStatusChanged(fbl::RefPtr<PortStatusChangeState> state) {
  if (state->port_index < state->port_count) {
    hci_->ScheduleTask(HandlePortStatusChangeEvent(static_cast<uint8_t>(state->port_index))
                           .then([=](fpromise::result<TRB*, zx_status_t>& trb)
                                     -> fpromise::result<TRB*, zx_status_t> {
                             if (trb.is_error()) {
                               if (trb.error() == ZX_ERR_BAD_STATE) {
                                 return trb;
                               }
                             }
                             state->port_index++;
                             CallPortStatusChanged(state);
                             return fpromise::ok(nullptr);
                           })
                           .box());
  } else {
    if (enumeration_queue_.is_empty()) {
      enumerating_ = false;
    } else {
      enumerating_ = true;
      auto enum_task = enumeration_queue_.pop_front();
      hci_->ScheduleTask(HandlePortStatusChangeEvent(enum_task->port_number)
                             .then([this, state, task = std::move(enum_task)](
                                       fpromise::result<TRB*, zx_status_t>& trb) mutable {
                               if (trb.is_error()) {
                                 if (trb.error() == ZX_ERR_BAD_STATE) {
                                   return trb;
                                 }
                                 task->completer->complete_error(trb.error());
                               } else {
                                 task->completer->complete_ok(trb.value());
                               }
                               state->port_index = state->port_count;
                               CallPortStatusChanged(state);
                               return trb;
                             }));
    }
  }
}

void EventRing::HandlePortStatusChangeEventInterrupt(uint8_t port_id, bool preempt) {
  auto ctx = hci_->get_command_ring()->AllocateContext();
  ctx->port_number = port_id;
  fpromise::bridge<TRB*, zx_status_t> bridge;
  ctx->completer = std::move(bridge.completer);
  hci_->ScheduleTask(bridge.consumer.promise()
                         .then([=](fpromise::result<TRB*, zx_status_t>& result) { return result; })
                         .box());
  if (preempt) {
    enumeration_queue_.push_front(std::move(ctx));
  } else {
    enumeration_queue_.push_back(std::move(ctx));
  }
  if (!enumerating_) {
    auto state = fbl::MakeRefCounted<PortStatusChangeState>(0, 0);
    CallPortStatusChanged(std::move(state));
  }
}

zx_status_t EventRing::Ring0Bringup() {
  hci_->WaitForBringup();
  enumerating_ = false;
  return ZX_OK;
}

void EventRing::ScheduleTask(fpromise::promise<TRB*, zx_status_t> promise) {
  {
    auto continuation = promise.then([=](fpromise::result<TRB*, zx_status_t>& result) {
      if (result.is_error()) {
        // ZX_ERR_BAD_STATE is a special value that we use to signal
        // a fatal error in xHCI. When this occurs, we should immediately
        // attempt to shutdown the controller. This error cannot be recovered from.
        if (result.error() == ZX_ERR_BAD_STATE) {
          hci_->Shutdown(ZX_ERR_BAD_STATE);
        }
      }
      return result;
    });
    executor_.schedule_task(std::move(continuation));
  }
}

void EventRing::RunUntilIdle() { executor_.run_until_idle(); }

std::variant<bool, std::unique_ptr<TRBContext>> EventRing::StallWorkaroundForDefectiveHubs(
    std::unique_ptr<TRBContext> context) {
  // Workaround for full-speed hub issue in Gateway keyboard
  auto request = context->request->request();
  if ((request->header.ep_address == 0) && (request->setup.b_request == USB_REQ_GET_DESCRIPTOR) &&
      (request->setup.w_index == 0) && (request->setup.w_value == (USB_DT_DEVICE_QUALIFIER << 8))) {
    usb_device_qualifier_descriptor_t* desc;
    if ((context->request->Mmap(reinterpret_cast<void**>(&desc)) == ZX_OK) &&
        (request->header.length >= sizeof(desc))) {
      desc->b_device_protocol =
          0;  // Don't support multi-TT unless we're sure the device supports it.
      hci_->ScheduleTask(hci_->UsbHciResetEndpointAsync(request->header.device_id, 0)
                             .and_then([ctx = std::move(context)](TRB*& result) {
                               ctx->request->Complete(ZX_OK, sizeof(*desc));
                               return fpromise::ok(result);
                             }));

      return true;
    }
  }
  return context;
}

zx_status_t EventRing::HandleIRQ() {
  iman_reg_.set_IP(1).set_IE(1).WriteTo(mmio_);
  bool avoid_yield = false;
  zx_paddr_t last_phys = 0;
  // avoid_yield is used to indicate that we are in "realtime mode"
  // When in this mode, we should avoid yielding our timeslice to the scheduler
  // if at all possible, because yielding could result in us getting behind on our
  // deadlines. Currently; we only ever need this on systems
  // that don't support cache coherency where we may have to go through the loop
  // several times due to stale values in the cache (after invalidating of course).
  // On systems with a coherent cache this isn't necessary.
  // Additionally; if we had a guarantee from the scheduler that we
  // would be woken up in <125 microseconds (length of USB frame),
  // we could safely yield after flushing our caches and wouldn't need this loop.
  do {
    avoid_yield = false;
    for (Control control = Control::FromTRB(erdp_virt_); control.Cycle() == ccs_;
         control = AdvanceErdp()) {
      switch (control.Type()) {
        case Control::PortStatusChangeEvent: {
          // Section 4.3 -- USB device intialization
          // Section 6.4.2.3 (Port Status change TRB)
          auto change_event = static_cast<PortStatusChangeEvent*>(erdp_virt_);
          uint8_t port_id = static_cast<uint8_t>(change_event->PortID());
          auto event = std::move(hci_->get_port_state()[port_id - 1].wait_for_port_status_change_);
          // Resume interrupted wait
          if (event) {
            event->completer->complete_ok(nullptr);
          } else {
            HandlePortStatusChangeEventInterrupt(port_id);
          }
        } break;
        case Control::CommandCompletionEvent: {
          auto completion_event = static_cast<CommandCompletionEvent*>(erdp_virt_);
          if (completion_event->CompletionCode() != CommandCompletionEvent::Success) {
          }
          TRB* trb = command_ring_->PhysToVirt(erdp_virt_->ptr);
          // Advance dequeue pointer
          std::unique_ptr<TRBContext> context;
          zx_status_t status = command_ring_->CompleteTRB(trb, &context);
          if (status != ZX_OK) {
            hci_->Shutdown(ZX_ERR_BAD_STATE);
            return ZX_ERR_BAD_STATE;
          }
          if (status != ZX_OK) {
            hci_->Shutdown(status);
            return status;
          }
          if (completion_event->CompletionCode() == CommandCompletionEvent::SlotNotEnabledError) {
            break;
          }
          // Invoke the callback to pre-process the command first.
          // The command MAY mutate the state of the completion event.
          // It is important that it be called prior to further processing of the event.
          if (context->completer.has_value()) {
            context->completer.value().complete_ok(completion_event);
          }
        } break;
        case Control::TransferEvent: {
          auto completion = static_cast<TransferEvent*>(erdp_virt_);
          auto state = &hci_->get_device_state()[completion->SlotID() - 1];
          fbl::AutoLock l(&state->transaction_lock());
          std::unique_ptr<TRBContext> context;
          TransferRing* ring;
          uint8_t endpoint_id = static_cast<uint8_t>(completion->EndpointID() - 1);
          if (unlikely(endpoint_id == 0)) {
            ring = &state->GetTransferRing();
          } else {
            ring = &state->GetTransferRing(endpoint_id - 1);
          }
          if (completion->CompletionCode() == CommandCompletionEvent::RingOverrun) {
            break;
          }
          if (completion->CompletionCode() == CommandCompletionEvent::RingUnderrun) {
            break;
          }
          TRB* trb;
          if (unlikely(!erdp_virt_->ptr || (completion->CompletionCode() ==
                                            CommandCompletionEvent::EndpointNotEnabledError))) {
            trb = nullptr;
          } else {
            trb = ring->PhysToVirt(erdp_virt_->ptr);
          }
          if (completion->CompletionCode() == CommandCompletionEvent::MissedServiceError) {
            if (!trb) {
              break;
            }
          }

          zx_status_t status = ZX_ERR_IO;
          size_t short_transfer_len = 0;
          TRB* first_trb = trb;
          if (trb) {
            if (completion->CompletionCode() == CommandCompletionEvent::ShortPacket) {
              ring->HandleShortPacket(trb, &short_transfer_len, &first_trb,
                                      completion->TransferLength());
              if (trb != first_trb) {
                // We'll get a second event for this TRB -- but we need to log the fact that this
                // was a short transfer.
                break;
              }
            }
            status = ring->CompleteTRB(first_trb, &context);
            if (status == ZX_ERR_IO && ring->IsIsochronous()) {
              // Out-of-order callback on isochronous ring.
              // This is a very special case where a transfer fails
              // and the HCI ends up missing several intervening TRBs
              // because we couldn't fill the ring fast enough.
              // As a workaround; we complete TRBs up to and including
              // the failed TRB, and update the dequeue pointer
              // to point to the last known transaction.
              // Section 4.10.3.2 says that controllers should give us a valid
              // pointer during the missed service event, but in practice; they all
              // just return zero.
              auto completions = ring->TakePendingTRBsUntil(trb);
              l.release();
              for (auto cb = completions.begin(); cb != completions.end(); cb++) {
                cb->request->Complete(ZX_ERR_IO, 0);
              }
              ring->ResetShortCount();
              context->request->Complete(ZX_ERR_IO, 0);
              break;
            }
          }
          if (completion->CompletionCode() == CommandCompletionEvent::StallError) {
            ring->set_stall(true);
            auto completions = ring->TakePendingTRBs();
            l.release();
            if (context) {
              if (completions.is_empty()) {
                auto result = StallWorkaroundForDefectiveHubs(std::move(context));
                if (std::holds_alternative<bool>(result)) {
                  break;
                }
                context = std::move(std::get<std::unique_ptr<TRBContext>>(result));
              }
              context->request->Complete(ZX_ERR_IO_REFUSED, 0);
            }
            for (auto cb = completions.begin(); cb != completions.end(); cb++) {
              cb->request->Complete(ZX_ERR_IO_REFUSED, 0);
            }

            break;
          }
          if (status != ZX_OK) {
            auto completions = ring->TakePendingTRBs();
            l.release();
            if (context) {
              context->request->Complete(ZX_ERR_IO, 0);
            }
            for (auto cb = completions.begin(); cb != completions.end(); cb++) {
              cb->request->Complete(ZX_ERR_IO, 0);
            }
            ring->ResetShortCount();
            // NOTE: No need to shutdown the whole slot.
            // It may only be an endpoint-specific failure.
            break;
          }
          l.release();

          if ((completion->CompletionCode() != CommandCompletionEvent::Success) &&
              (completion->CompletionCode() != CommandCompletionEvent::ShortPacket)) {
            // asix-88179 will stall the endpoint if we're sending data too fast.
            // The driver expects us to give it a ZX_ERR_IO_INVALID response when
            // this happens.
            context->request->Complete(ZX_ERR_IO_INVALID, 0);
            break;
          }
          if (context->short_length || context->transfer_len_including_short_trb) {
            context->request->Complete(
                ZX_OK, context->transfer_len_including_short_trb - context->short_length);
          } else {
            context->request->Complete(ZX_OK, context->request->request()->header.length);
          }
          ring->ResetShortCount();
        } break;
        case Control::MFIndexWrapEvent: {
          hci_->MfIndexWrapped();
        } break;
        case Control::HostControllerEvent: {
          // NOTE: We can't really do anything here.
          // This typically indicates some kind of error condition.
          // If something strange is happening, it might be a good idea
          // to add a printf here and log the completion code.
        } break;
      }
    }
    if (last_phys != erdp_phys_) {
      hci_->RunUntilIdle();
      erdp_reg_ =
          erdp_reg_.set_Pointer(erdp_phys_).set_DESI(segment_index_).set_EHB(1).WriteTo(mmio_);
      last_phys = erdp_phys_;
    }
    if (!hci_->HasCoherentState()) {
      // Check for stale value in cache
      InvalidatePageCache(erdp_virt_, ZX_CACHE_FLUSH_INVALIDATE | ZX_CACHE_FLUSH_DATA);
      if (Control::FromTRB(erdp_virt_).Cycle() == ccs_) {
        avoid_yield = true;
      }
    }
  } while (avoid_yield);
  return ZX_OK;
}

TRBPromise EventRing::LinkUp(uint8_t port_id) {
  // Port is in U0 state (link up)
  // Enumerate device
  return EnumerateDevice(hci_, port_id, std::nullopt);
}

void EventRing::Usb2DeviceAttach(uint16_t port_id) {
  hci_->get_port_state()[port_id - 1].is_connected = true;
  hci_->get_port_state()[port_id - 1].is_USB3 = false;
  auto sc = PORTSC::Get(cap_length_, port_id).ReadFrom(mmio_);
  PORTSC::Get(cap_length_, port_id)
      .FromValue(0)
      .set_CCS(sc.CCS())
      .set_PortSpeed(sc.PortSpeed())
      .set_PIC(sc.PIC())
      .set_PLS(sc.PLS())
      .set_PP(sc.PP())
      .set_PR(1)
      .WriteTo(mmio_);
}

void EventRing::Usb3DeviceAttach(uint16_t port_id) {
  hci_->get_port_state()[port_id - 1].is_connected = true;
  hci_->get_port_state()[port_id - 1].is_USB3 = true;
}

}  // namespace usb_xhci
