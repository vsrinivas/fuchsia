// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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

    int IrqLoop();
private:
    void EnableHotplugInterrupts();
    void HandlePipeInterrupt(registers::Pipe pipe);

    bool pipe_vsyncs_[registers::kPipeCount];

    Controller* controller_;

    zx::handle irq_;
    thrd_t irq_thread_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Interrupts);
};

} // namespace i915
