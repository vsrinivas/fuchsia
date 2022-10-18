// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_INTERRUPTS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_INTERRUPTS_H_

#include <fuchsia/hardware/intelgpucore/c/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/fit/function.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/types.h>

#include <bitset>
#include <optional>

#include <fbl/macros.h>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"

namespace i915_tgl {

class Interrupts {
 public:
  // Callbacks that are invoked on various interrupt types.
  //
  // All interrupt callbacks are currently run on the same thread (the internal
  // thread dedicated to handling interrupt). However, implementations must be
  // thread-safe, and not rely on any assumptions around the threading model.
  using PipeVsyncCallback = fit::function<void(tgl_registers::Pipe, zx_time_t)>;
  using HotplugCallback = fit::function<void(tgl_registers::Ddi ddi, bool long_pulse)>;

  Interrupts();
  ~Interrupts();

  // The lifetimes of |dev|, |pci|, and |mmio_space| must outlast the initialized Interrupts
  // instance.
  zx_status_t Init(PipeVsyncCallback pipe_vsync_callback, HotplugCallback hotplug_callback,
                   zx_device_t* dev, const ddk::Pci& pci, fdf::MmioBuffer* mmio_space,
                   uint16_t device_id);
  void FinishInit();
  void Resume();
  void Destroy();

  // Enable or disable interrupt generation from `pipe`.
  //
  // This method enables and disables all the pipe-level interrupts that we are
  // prepared to handle.
  //
  // Transcoder VSync (vertical sync) interrupts trigger callbacks to the
  // PipeVsyncCallback provided to `Init()`. The callbacks are performed on the
  // internal thread dedicated to interrupt handling.
  void EnablePipeInterrupts(tgl_registers::Pipe pipe, bool enable);

  // The GPU driver uses this to plug into the interrupt stream.
  //
  // On Tiger Lake, `gpu_callback` will be called during an interrupt
  // from the graphics hardware if the Graphics Primary Interrupt register
  // indicates there are GT interrupts pending.
  //
  // On Skylake and Kaby Lake, `gpu_callback` will be called during an interrupt
  // from the graphics hardware if the Display Interrupt Control register has
  // any bits in `gpu_interrupt_mask` set.
  zx_status_t SetGpuInterruptCallback(const intel_gpu_core_interrupt_t& gpu_interrupt_callback,
                                      uint32_t gpu_interrupt_mask);

 private:
  int IrqLoop();
  void EnableHotplugInterrupts();
  void HandlePipeInterrupt(tgl_registers::Pipe pipe, zx_time_t timestamp);

  PipeVsyncCallback pipe_vsync_callback_;
  HotplugCallback hotplug_callback_;
  fdf::MmioBuffer* mmio_space_ = nullptr;

  mtx_t lock_;

  // Initialized by |Init|.
  zx::interrupt irq_;
  fuchsia_hardware_pci::InterruptMode irq_mode_;
  std::optional<thrd_t> irq_thread_;  // Valid while irq_ is valid.
  uint16_t device_id_;

  intel_gpu_core_interrupt_t gpu_interrupt_callback_ __TA_GUARDED(lock_) = {};
  uint32_t gpu_interrupt_mask_ __TA_GUARDED(lock_) = 0;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Interrupts);
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_INTERRUPTS_H_
