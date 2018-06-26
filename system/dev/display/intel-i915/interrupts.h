// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/intel-gpu-core.h>
#include <fbl/macros.h>
#include <threads.h>
#include <zircon/types.h>
#include <lib/zx/handle.h>

#include "registers-pipe.h"

namespace i915 {

class Controller;

class Interrupts {
public:
    Interrupts();
    ~Interrupts();

    zx_status_t Init(Controller* controller);
    void FinishInit();
    void Resume();
    void Destroy();

    void EnablePipeVsync(registers::Pipe pipe, bool enable);
    zx_status_t SetInterruptCallback(zx_intel_gpu_core_interrupt_callback_t callback,
                                     void* data, uint32_t interrupt_mask);

    int IrqLoop();
private:
    void EnableHotplugInterrupts();
    void HandlePipeInterrupt(registers::Pipe pipe, zx_time_t timestamp);

    Controller* controller_; // Assume that controller callbacks are threadsafe

    zx::handle irq_;
    thrd_t irq_thread_;

    zx_intel_gpu_core_interrupt_callback_t interrupt_cb_ __TA_GUARDED(lock_);
    void* interrupt_cb_data_ __TA_GUARDED(lock_);
    uint32_t interrupt_mask_ __TA_GUARDED(lock_) = 0;

    mtx_t lock_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Interrupts);
};

} // namespace i915
