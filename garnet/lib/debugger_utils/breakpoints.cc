// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "breakpoints.h"

#include <zircon/syscalls/debug.h>

#include <src/lib/fxl/logging.h>

#include "garnet/lib/debugger_utils/registers.h"
#include "garnet/lib/debugger_utils/util.h"

namespace debugger_utils {

zx_status_t ResumeAfterSoftwareBreakpointInstruction(
    zx_handle_t thread, zx_handle_t eport) {
  // Adjust the PC to point after the breakpoint instruction.
  zx_thread_state_general_regs_t regs;
  zx_status_t status = ReadGeneralRegisters(thread, &regs);
  if (status != ZX_OK) {
    return status;
  }
  zx_vaddr_t pc = GetPcFromGeneralRegisters(&regs);
  pc = IncrementPcAfterBreak(pc);
  SetPcInGeneralRegisters(&regs, pc);
  status = WriteGeneralRegisters(thread, &regs);
  if (status != ZX_OK) {
    return status;
  }

  // Now we can resume the thread.
  status = zx_task_resume_from_exception(thread, eport, 0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to resume thread " << GetKoid(thread)
                   << " after exception: "
                   << debugger_utils::ZxErrorString(status);
  }
  return status;
}

}  // namespace debugger_utils
