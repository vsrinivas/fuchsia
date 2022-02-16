// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_INTERRUPTER_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_INTERRUPTER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/device-protocol/pci.h>
#include <lib/zx/interrupt.h>

#include <thread>

#include "xhci-event-ring.h"

namespace usb_xhci {

// An interrupter that manages an event ring, and handles interrupts.
class Interrupter {
 public:
  Interrupter() = default;

  ~Interrupter() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  zx_status_t Init(uint16_t interrupter, size_t page_size, ddk::MmioBuffer* buffer,
                   const RuntimeRegisterOffset& offset, uint32_t erst_max,
                   DoorbellOffset doorbell_offset, UsbXhci* hci, HCCPARAMS1 hcc_params_1,
                   uint64_t* dcbaa);

  zx_status_t Start(const RuntimeRegisterOffset& offset, ddk::MmioView interrupter_regs);

  void Stop() {
    if (!active_) {
      // Already inactive;
      return;
    }
    active_ = false;
    if (async_executor_.has_value()) {
      async_executor_.value().schedule_task(fpromise::make_ok_promise().then(
          [=](fpromise::result<void, void>& result) { async_loop_->Quit(); }));
    }
  }

  EventRing& ring() { return event_ring_; }
  bool active() { return active_; }

  // Returns a pointer to the IRQ
  // owned by this interrupter
  zx::interrupt& GetIrq() { return irq_; }

  TRBPromise Timeout(zx::time deadline);

 private:
  std::atomic_bool active_ = false;
  uint16_t interrupter_;
  zx_status_t IrqThread();
  zx::interrupt irq_;
  std::thread thread_;
  EventRing event_ring_;
  std::optional<async::Executor> async_executor_;
  std::optional<async::Loop> async_loop_;
  // Reference to the xHCI core. Since Interrupter is a part of the
  // UsbXhci (always instantiated as a class member), this reference
  // will always be valid for the lifetime of the Interrupter.
  UsbXhci* hci_ = nullptr;
};

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_XHCI_INTERRUPTER_H_
