// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_INTERRUPTS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_INTERRUPTS_H_

#include <fuchsia/hardware/intelgpucore/c/banjo.h>
#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <lib/fit/function.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

#include <optional>

#include <fbl/macros.h>

#include "registers-ddi.h"
#include "registers-pipe.h"

namespace i915 {

class Interrupts {
 public:
  // Callbacks that are invoked on various interrupt types. All callbacks are run on the internal
  // irq thread and implementations must guarantee their own thread-safety.
  using PipeVsyncCallback = fit::function<void(registers::Pipe, zx_time_t)>;
  using HotplugCallback = fit::function<void(registers::Ddi ddi, bool long_pulse)>;

  Interrupts();
  ~Interrupts();

  // The lifetimes of |dev|, |pci|, and |mmio_space| must outlast the initialized Interrupts
  // instance.
  zx_status_t Init(PipeVsyncCallback pipe_vsync_callback, HotplugCallback hotplug_callback,
                   zx_device_t* dev, const pci_protocol_t* pci, ddk::MmioBuffer* mmio_space);
  void FinishInit();
  void Resume();
  void Destroy();

  // Initiate or stop vsync interrupt delivery from the given |pipe|. When enabled, interrupts will
  // be notified on the internal irq thread via the PipeVsyncCallback that was provided in Init.
  void EnablePipeVsync(registers::Pipe pipe, bool enable);

  zx_status_t SetInterruptCallback(const intel_gpu_core_interrupt_t* callback,
                                   uint32_t interrupt_mask);

 private:
  int IrqLoop();
  void EnableHotplugInterrupts();
  void HandlePipeInterrupt(registers::Pipe pipe, zx_time_t timestamp);

  PipeVsyncCallback pipe_vsync_callback_;
  HotplugCallback hotplug_callback_;
  ddk::MmioBuffer* mmio_space_ = nullptr;

  mtx_t lock_;

  // Initialized by |Init|.
  zx::interrupt irq_;
  pci_irq_mode_t irq_mode_;
  std::optional<thrd_t> irq_thread_;  // Valid while irq_ is valid.

  intel_gpu_core_interrupt_t interrupt_cb_ __TA_GUARDED(lock_) = {};
  uint32_t interrupt_mask_ __TA_GUARDED(lock_) = 0;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Interrupts);
};

}  // namespace i915

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_INTERRUPTS_H_
