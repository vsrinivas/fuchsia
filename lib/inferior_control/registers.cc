// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "garnet/lib/debugger_utils/util.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "thread.h"

namespace debugserver {

Registers::Registers(Thread* thread) : thread_(thread) {
  FXL_DCHECK(thread);
  FXL_DCHECK(thread->handle() != ZX_HANDLE_INVALID);
}

bool Registers::RefreshGeneralRegisters() {
  return RefreshRegset(ZX_THREAD_STATE_GENERAL_REGS);
}

bool Registers::WriteGeneralRegisters() {
  return WriteRegset(ZX_THREAD_STATE_GENERAL_REGS);
}

std::string Registers::GetGeneralRegistersAsString() {
  return GetRegsetAsString(ZX_THREAD_STATE_GENERAL_REGS);
}

bool Registers::SetGeneralRegistersFromString(const fxl::StringView& value) {
  return SetRegsetFromString(ZX_THREAD_STATE_GENERAL_REGS, value);
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
                   << ZxErrorString(status);
    return false;
  }

  FXL_VLOG(1) << "Regset " << regset << " refreshed";
  return true;
}

bool Registers::WriteRegsetHelper(int regset, const void* buf,
                                  size_t buf_size) {
  zx_status_t status =
      zx_thread_write_state(thread()->handle(), regset, buf, buf_size);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to write regset " << regset << ": "
                   << ZxErrorString(status);
    return false;
  }

  FXL_VLOG(1) << "Regset " << regset << " written";
  return true;
}

bool Registers::SetRegsetFromStringHelper(int regset, void* buffer,
                                          size_t buf_size,
                                          const fxl::StringView& value) {
  auto bytes = DecodeByteArrayString(value);
  if (bytes.size() != buf_size) {
    FXL_LOG(ERROR) << "|value| doesn't match regset " << regset << " size of "
                   << buf_size << ": " << value;
    return false;
  }

  memcpy(buffer, bytes.data(), buf_size);
  FXL_VLOG(1) << "Regset " << regset << " cache written";
  return true;
}

zx_vaddr_t Registers::GetIntRegister(int regno) {
  zx_vaddr_t value;
  bool success = GetRegister(regno, &value, sizeof(value));
  FXL_DCHECK(success);
  return value;
}

zx_vaddr_t Registers::GetPC() { return GetIntRegister(GetPCRegisterNumber()); }

zx_vaddr_t Registers::GetSP() { return GetIntRegister(GetSPRegisterNumber()); }

zx_vaddr_t Registers::GetFP() { return GetIntRegister(GetFPRegisterNumber()); }

}  // namespace debugserver
