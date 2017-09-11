// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>

#include "debugger-utils/util.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "thread.h"

namespace debugserver {
namespace arch {

Registers::Registers(Thread* thread) : thread_(thread) {
  FXL_DCHECK(thread);
  FXL_DCHECK(thread->handle() != MX_HANDLE_INVALID);
}

bool Registers::RefreshGeneralRegisters() {
  return RefreshRegset(MX_THREAD_STATE_REGSET0);
}

bool Registers::WriteGeneralRegisters() {
  return WriteRegset(MX_THREAD_STATE_REGSET0);
}

std::string Registers::GetGeneralRegistersAsString() {
  return GetRegsetAsString(MX_THREAD_STATE_REGSET0);
}

bool Registers::SetGeneralRegistersFromString(const fxl::StringView& value) {
  return SetRegsetFromString(MX_THREAD_STATE_REGSET0, value);
}

bool Registers::RefreshRegsetHelper(int regset, void* buf, size_t buf_size) {
  // We report all zeros for the registers if the thread was just created.
  if (thread()->state() == Thread::State::kNew) {
    memset(buf, 0, buf_size);
    return true;
  }

  uint32_t regset_size;
  mx_status_t status = mx_thread_read_state(
    thread()->handle(), regset, buf, buf_size, &regset_size);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to read regset " << regset << ": "
                   << util::MxErrorString(status);
    return false;
  }

  FXL_DCHECK(regset_size == buf_size);

  FXL_VLOG(1) << "Regset " << regset << " refreshed";
  return true;
}

bool Registers::WriteRegsetHelper(int regset, const void* buf,
                                  size_t buf_size) {
  mx_status_t status = mx_thread_write_state(thread()->handle(), regset,
                                             buf, buf_size);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to write regset " << regset << ": "
                   << util::MxErrorString(status);
    return false;
  }

  FXL_VLOG(1) << "Regset " << regset << " written";
  return true;
}

bool Registers::SetRegsetFromStringHelper(int regset,
                                          void* buffer, size_t buf_size,
                                          const fxl::StringView& value) {
  auto bytes = util::DecodeByteArrayString(value);
  if (bytes.size() != buf_size) {
    FXL_LOG(ERROR) << "|value| doesn't match regset " << regset << " size of "
                   << buf_size << ": " << value;
    return false;
  }

  memcpy(buffer, bytes.data(), buf_size);
  FXL_VLOG(1) << "Regset " << regset << " cache written";
  return true;
}

mx_vaddr_t Registers::GetIntRegister(int regno) {
  mx_vaddr_t value;
  bool success = GetRegister(regno, &value, sizeof(value));
  FXL_DCHECK(success);
  return value;
}

mx_vaddr_t Registers::GetPC() {
  return GetIntRegister(GetPCRegisterNumber());
}

mx_vaddr_t Registers::GetSP() {
  return GetIntRegister(GetSPRegisterNumber());
}

mx_vaddr_t Registers::GetFP() {
  return GetIntRegister(GetFPRegisterNumber());
}

}  // namespace arch
}  // namespace debugserver
