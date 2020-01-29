// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-event-ring.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/irq.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/promise.h>
#include <lib/zx/clock.h>

#include <fbl/auto_call.h>

#include "usb-xhci.h"

namespace usb_xhci {

zx_status_t EventRingSegmentTable::Init(size_t page_size, const zx::bti& bti, bool is_32bit,
                                        uint32_t erst_max, ERSTSZ erst_size,
                                        ddk::MmioBuffer* mmio) {
  erst_size_ = erst_size;
  bti_ = &bti;
  page_size_ = page_size;
  is_32bit_ = is_32bit;
  mmio_.emplace(mmio->View(0));
  zx_status_t status = dma_buffer::PagedBuffer::Create(bti, ZX_PAGE_SIZE, false, &erst_);
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
  entry.address = paddr;
  entry.u.size = static_cast<uint16_t>(page_size_ / 16);
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
  return segments_.Init(page_size, bti, is_32bit, erst_max, erst_size, mmio_);
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
    std::optional<dma_buffer::ContiguousBuffer> buffer_tmp;
    zx_status_t status = dma_buffer::ContiguousBuffer::Create(
        *bti_, page_size_, static_cast<uint32_t>(page_size_ == PAGE_SIZE ? 0 : page_size_ >> 12),
        &buffer_tmp);
    if (status != ZX_OK) {
      return status;
    }
    buffer = std::make_unique<dma_buffer::ContiguousBuffer>(std::move(*buffer_tmp));
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

zx_status_t EventRing::HandlePortStatusChangeEvent(uint8_t port_id) {
  auto sc = PORTSC::Get(cap_length_, port_id).ReadFrom(mmio_);
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
    if (sc.CCS()) {
      // Wait for the port to exit polling state, if applicable.
      if (sc.PLS() == PORTSC::Polling) {
        // USB 2.0 port connect
        Usb2DeviceAttach(port_id);
      } else {
        // USB 3.0 port connect
        Usb3DeviceAttach(port_id);
        if (sc.PLS() == PORTSC::U0) {
          LinkUp(port_id);
        }
      }
      sc = PORTSC::Get(cap_length_, port_id).ReadFrom(mmio_);
    } else {
      // For hubs, we need to take the device offline from the bus's standpoint before tearing down
      // the hub. This means that the slot has to be kept alive until the hub driver is removed.
      if (!hci_->get_port_state()[port_id - 1].slot_id) {
        // No slot was bound to this port.
        return ZX_OK;
      }
      fbl::AutoLock l(&hci_->get_device_state()[hci_->get_port_state()[port_id - 1].slot_id - 1]
                           .transaction_lock());
      hci_->get_port_state()[port_id - 1].retry = false;
      hci_->get_port_state()[port_id - 1].link_active = false;
      hci_->get_port_state()[port_id - 1].is_connected = false;
      hci_->get_port_state()[port_id - 1].is_USB3 = false;
      l.release();
      ScheduleTask(hci_->DeviceOffline(hci_->get_port_state()[port_id - 1].slot_id, nullptr).box());
      return ZX_OK;
    }
  }
  if (sc.PEC()) {
    return ZX_ERR_BAD_STATE;
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
    sc = PORTSC::Get(cap_length_, port_id).ReadFrom(mmio_);
    // Link could be active from connect status change above.
    if ((sc.PLS() == PORTSC::U0) && sc.PED() && sc.CCS() &&
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
        hci_->ScheduleTask(hci_->Timeout(zx::deadline_after(zx::msec(10)))
                               .and_then([=](TRB*& result) {
                                 LinkUp(static_cast<uint8_t>(port_id));
                                 return fit::ok(result);
                               })
                               .box());
      } else {
        zx_status_t status = LinkUp(static_cast<uint8_t>(port_id));
        return status;
      }
    }
  }
  return ZX_OK;
}

TRBPromise Interrupter::Timeout(zx::time deadline) {
  fit::bridge<TRB*, zx_status_t> bridge;
  zx_status_t status = async::PostTaskForTime(
      async_loop_->dispatcher(),
      [completer = std::move(bridge.completer), this]() mutable {
        completer.complete_ok(nullptr);
        hci_->RunUntilIdle();
      },
      deadline);
  if (status != ZX_OK) {
    return hci_->ResultToTRBPromise(fit::error(status));
  }
  return bridge.consumer.promise().box();
}

zx_status_t Interrupter::IrqThread() {
  // TODO(ZX-940): See ZX-940.  Get rid of this.  For now we need thread
  // priorities so that realtime transactions use the completer which ends
  // up getting realtime latency guarantees.
  async_loop_config_t config = kAsyncLoopConfigNeverAttachToThread;
  config.irq_support = true;
  async_loop_.emplace(&config);
  async_executor_.emplace(async_loop_->dispatcher());
  if (zx_object_set_profile(zx_thread_self(), hci_->get_profile().get(), 0) != ZX_OK) {
    zxlogf(WARN,
           "No scheduler profile available to apply to the high priority XHCI completer.  "
           "Service will be best effort.\n");
  }
  async::Irq irq;
  irq.set_object(irq_.get());
  irq.set_handler([&](async_dispatcher_t* dispatcher, async::Irq* irq, zx_status_t status,
                      const zx_packet_interrupt_t* interrupt) {
    if (!irq_.is_valid()) {
      async_loop_->Quit();
    }
    if (status != ZX_OK) {
      async_loop_->Quit();
      return;
    }

    if (event_ring_.HandleIRQ() != ZX_OK) {
      zxlogf(ERROR, "Error handling IRQ. Exiting async loop.");
      async_loop_->Quit();
      return;
    }
    hci_->RunUntilIdle();
    irq_.ack();
  });
  irq.Begin(async_loop_->dispatcher());
  if (!interrupter_) {
    // Note: We need to run the ring 0 bringup after
    // initializing interrupts, since Qemu initialization
    // code assumes that interrupts are active and simulates
    // a port status changed event.
    if (event_ring_.Ring0Bringup()) {
      zxlogf(ERROR, "Failed to bring up ring 0");
      return ZX_ERR_INTERNAL;
    }
  }
  async_loop_->Run();
  return ZX_OK;
}

zx_status_t EventRing::Ring0Bringup() {
  hci_->WaitForBringup();
  // Qemu doesn't generate interrupts for already-connected devices.
  // In order to support USB passthrouh on Qemu, we need to simulate
  // a port status change event for each virtual port.
  if (hci_->IsQemu()) {
    for (size_t i = 0; i < hci_->get_port_count(); i++) {
      HandlePortStatusChangeEvent(static_cast<uint8_t>(i));
    }
  }
  return ZX_OK;
}

void EventRing::ScheduleTask(fit::promise<TRB*, zx_status_t> promise) {
  {
    auto continuation = promise.then([=](fit::result<TRB*, zx_status_t>& result) {
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

void EventRing::RunUntilIdle() { executor_.run(); }

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
          zx_status_t status = HandlePortStatusChangeEvent(port_id);
          if (status != ZX_OK) {
            if (status == ZX_ERR_BAD_STATE) {
              hci_->Shutdown(status);
              return status;
            }
            return status;
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
          if (trb) {
            status = ring->CompleteTRB(trb, &context);
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
          context->request->Complete(
              ZX_OK, context->request->request()->header.length - completion->TransferLength());
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

zx_status_t EventRing::LinkUp(uint8_t port_id) {
  // Port is in U0 state (link up)
  // Enumerate device
  ScheduleTask(EnumerateDevice(hci_, port_id, std::nullopt));
  return ZX_OK;
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
