// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>
#include <zircon/syscalls/debug.h>

#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/inferior_control/thread.h"

#include "registers.h"

namespace debugserver {

std::string GetGeneralRegistersAsString(Thread* thread) {
  return GetRegsetAsString(thread, ZX_THREAD_STATE_GENERAL_REGS);
}

bool SetRegsetHelper(Thread* thread, int regset, const void* value,
                     size_t size) {
  FXL_DCHECK(regset == 0);
  FXL_DCHECK(size == sizeof(zx_thread_state_general_regs_t));
  if (!thread->registers()->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Unable to refresh general registers";
    return false;
  }
  memcpy(thread->registers()->GetGeneralRegisters(), value, size);
  FXL_VLOG(2) << "Regset " << regset << " cache written";
  return true;
}

bool SetGeneralRegistersFromString(Thread* thread,
                                   const fxl::StringView& value) {
  return SetRegsetFromString(thread, ZX_THREAD_STATE_GENERAL_REGS, value);
}

}  // namespace debugserver
