// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-interrupter.h"

#include <lib/async/cpp/irq.h>
#include <lib/async/cpp/task.h>

#include "usb-xhci.h"

namespace usb_xhci {

zx_status_t Interrupter::Init(uint16_t interrupter, size_t page_size, fdf::MmioBuffer* buffer,
                              const RuntimeRegisterOffset& offset, uint32_t erst_max,
                              DoorbellOffset doorbell_offset, UsbXhci* hci, HCCPARAMS1 hcc_params_1,
                              uint64_t* dcbaa) {
  if (active_) {
    // Already active;
    return ZX_OK;
  }
  hci_ = hci;
  interrupter_ = interrupter;
  return event_ring_.Init(page_size, hci_->bti(), buffer, hci->Is32BitController(), erst_max,
                          ERSTSZ::Get(offset, interrupter_).ReadFrom(buffer),
                          ERDP::Get(offset, interrupter_).ReadFrom(buffer),
                          IMAN::Get(offset, interrupter_).FromValue(0), hci_->CapLength(),
                          HCSPARAMS1::Get().ReadFrom(buffer), hci_->GetCommandRing(),
                          doorbell_offset, hci, hcc_params_1, dcbaa, interrupter_);
}

zx_status_t Interrupter::Start(const RuntimeRegisterOffset& offset, fdf::MmioView mmio_view) {
  if (active_) {
    // Already active;
    return ZX_OK;
  }
  ERDP erdp = ERDP::Get(offset, interrupter_).ReadFrom(&mmio_view);
  if (!event_ring_.erdp_phys()) {
    return ZX_ERR_BAD_STATE;
  }
  erdp.set_reg_value(event_ring_.erdp_phys());
  erdp.WriteTo(&mmio_view);
  ERSTBA ba = ERSTBA::Get(offset, interrupter_).ReadFrom(&mmio_view);
  // This enables the interrupter
  ba.set_Pointer(event_ring_.erst()).WriteTo(&mmio_view);
  IMAN::Get(offset, interrupter_).FromValue(0).set_IE(1).WriteTo(&mmio_view);
  thread_ = std::thread([this]() { IrqThread(); });
  active_ = true;
  return ZX_OK;
}

TRBPromise Interrupter::Timeout(zx::time deadline) {
  fpromise::bridge<TRB*, zx_status_t> bridge;
  zx_status_t status = async::PostTaskForTime(
      async_loop_->dispatcher(),
      [completer = std::move(bridge.completer), this]() mutable {
        completer.complete_ok(nullptr);
        hci_->RunUntilIdle(interrupter_);
      },
      deadline);
  if (status != ZX_OK) {
    return fpromise::make_error_promise(status);
  }
  return bridge.consumer.promise().box();
}

zx_status_t Interrupter::IrqThread() {
  // TODO(fxbug.dev/30888): See fxbug.dev/30888.  Get rid of this.  For now we need thread
  // priorities so that realtime transactions use the completer which ends
  // up getting realtime latency guarantees.
  async_loop_config_t config = kAsyncLoopConfigNeverAttachToThread;
  config.irq_support = true;
  async_loop_.emplace(&config);
  async_executor_.emplace(async_loop_->dispatcher());
  if (zx_object_set_profile(zx_thread_self(), hci_->get_profile().get(), 0) != ZX_OK) {
    zxlogf(WARNING,
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

}  // namespace usb_xhci
