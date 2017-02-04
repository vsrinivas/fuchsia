// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <magenta/syscalls/debug.h>

#include "lib/ftl/logging.h"

#include "thread.h"

namespace debugserver {
namespace arch {

Registers::Registers(Thread* thread) : thread_(thread) {
  FTL_DCHECK(thread);
  FTL_DCHECK(thread->handle() != MX_HANDLE_INVALID);
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

bool Registers::SetGeneralRegisters(const ftl::StringView& value) {
  return SetRegset(MX_THREAD_STATE_REGSET0, value);
}

mx_vaddr_t Registers::GetIntRegister(int regno) {
  mx_vaddr_t value;
  bool success = GetRegister(regno, &value, sizeof(value));
  FTL_DCHECK(success);
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
