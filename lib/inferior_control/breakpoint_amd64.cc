// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoint.h"

#include "lib/fxl/logging.h"

#include "process.h"
#include "registers.h"
#include "thread.h"

namespace debugserver {

namespace {

// The i386 Int3 instruction.
const uint8_t kInt3 = 0xCC;

}  // namespace

bool SoftwareBreakpoint::Insert() {
  // TODO: Handle breakpoints in unloaded solibs.

  if (IsInserted()) {
    FXL_LOG(WARNING) << "Breakpoint already inserted";
    return false;
  }

  // We only support inserting the single byte Int3 instruction.
  if (kind() != 1) {
    FXL_LOG(ERROR) << "Software breakpoint kind must be 1 on amd64";
    return false;
  }

  // Read the current contents at the address that we're about to overwrite, so
  // that it can be restored later.
  uint8_t orig;
  if (!owner()->process()->ReadMemory(address(), &orig, 1)) {
    FXL_LOG(ERROR) << "Failed to obtain current contents of memory";
    return false;
  }

  // Insert the Int3 instruction.
  if (!owner()->process()->WriteMemory(address(), &kInt3, 1)) {
    FXL_LOG(ERROR) << "Failed to insert software breakpoint";
    return false;
  }

  original_bytes_.push_back(orig);
  return true;
}

bool SoftwareBreakpoint::Remove() {
  if (!IsInserted()) {
    FXL_LOG(WARNING) << "Breakpoint not inserted";
    return false;
  }

  FXL_DCHECK(original_bytes_.size() == 1);

  // Restore the original contents.
  if (!owner()->process()->WriteMemory(address(), original_bytes_.data(), 1)) {
    FXL_LOG(ERROR) << "Failed to restore original instructions";
    return false;
  }

  original_bytes_.clear();
  return true;
}

bool SoftwareBreakpoint::IsInserted() const { return !original_bytes_.empty(); }

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

  if (!SetRflagsTF(owner()->thread(), true)) return false;

  inserted_ = true;
  return true;
}

bool SingleStepBreakpoint::Remove() {
  if (!IsInserted()) {
    FXL_LOG(WARNING) << "Breakpoint not inserted";
    return false;
  }

  if (!SetRflagsTF(owner()->thread(), false)) return false;

  inserted_ = false;
  return true;
}

bool SingleStepBreakpoint::IsInserted() const { return inserted_; }

}  // namespace debugserver
