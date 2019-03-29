// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/lib/debugger_utils/breakpoints.h"

#include "breakpoint.h"
#include "process.h"
#include "registers.h"
#include "thread.h"

namespace inferior_control {

namespace {

// Set the TF bit in the RFLAGS register of |thread|.

bool SetRflagsTF(Thread* thread, bool enable) {
  Registers* registers = thread->registers();

  if (!registers->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Failed to refresh general regs";
    return false;
  }
  if (!registers->SetSingleStep(enable)) {
    FXL_LOG(ERROR) << "Failed to set rflags.TF";
    return false;
  }
  if (!registers->WriteGeneralRegisters()) {
    FXL_LOG(ERROR) << "Failed to write general regs";
    return false;
  }

  return true;
}

}  // anonymous namespace

bool SingleStepBreakpoint::Insert() {
  if (IsInserted()) {
    FXL_LOG(WARNING) << "Breakpoint already inserted";
    return false;
  }

  // TODO: Manage things like the user having already set TF.

  if (!SetRflagsTF(owner()->thread(), true))
    return false;

  inserted_ = true;
  return true;
}

bool SingleStepBreakpoint::Remove() {
  if (!IsInserted()) {
    FXL_LOG(WARNING) << "Breakpoint not inserted";
    return false;
  }

  if (!SetRflagsTF(owner()->thread(), false))
    return false;

  inserted_ = false;
  return true;
}

bool SingleStepBreakpoint::IsInserted() const { return inserted_; }

}  // namespace inferior_control
