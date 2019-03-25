// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "garnet/lib/debugger_utils/util.h"

#include "registers.h"
#include "thread.h"

namespace inferior_control {

Registers::Registers(Thread* thread) : thread_(thread) {
  FXL_DCHECK(thread);
  FXL_DCHECK(thread->handle() != ZX_HANDLE_INVALID);
}

bool Registers::RefreshRegset(int regset) {
  FXL_DCHECK(regset == 0);
  return RefreshRegsetHelper(regset, &general_regs_, sizeof(general_regs_));
}

bool Registers::WriteRegset(int regset) {
  FXL_DCHECK(regset == 0);
  return WriteRegsetHelper(regset, &general_regs_, sizeof(general_regs_));
}

bool Registers::RefreshGeneralRegisters() {
  return RefreshRegset(ZX_THREAD_STATE_GENERAL_REGS);
}

bool Registers::WriteGeneralRegisters() {
  return WriteRegset(ZX_THREAD_STATE_GENERAL_REGS);
}

zx_thread_state_general_regs_t* Registers::GetGeneralRegisters() {
  return &general_regs_;
}

bool Registers::RefreshRegsetHelper(int regset, void* buf, size_t buf_size) {
  // We report all zeros for the registers if the thread was just created.
  if (thread()->state() == Thread::State::kNew) {
    memset(buf, 0, buf_size);
    return true;
  }

  zx_status_t status =
      zx_thread_read_state(thread()->handle(), regset, buf, buf_size);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to read regset " << regset << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  FXL_VLOG(4) << "Regset " << regset << " refreshed";
  return true;
}

bool Registers::WriteRegsetHelper(int regset, const void* buf,
                                  size_t buf_size) {
  zx_status_t status =
      zx_thread_write_state(thread()->handle(), regset, buf, buf_size);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to write regset " << regset << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  FXL_VLOG(4) << "Regset " << regset << " written";
  return true;
}

}  // namespace inferior_control
