// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>
#include <zircon/syscalls/debug.h>

#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/inferior_control/thread.h"

#include "registers.h"

namespace debugserver {

constexpr int kFpRegisterNumber = 6; // rbp
constexpr int kSpRegisterNumber = 7; // rsp
constexpr int kPcRegisterNumber = 16; // rip
constexpr int kNumGeneralRegisters = 18;

int GetFPRegisterNumber() { return kFpRegisterNumber; }

int GetSPRegisterNumber() { return kSpRegisterNumber; }

int GetPCRegisterNumber() { return kPcRegisterNumber; }

std::string GetUninitializedGeneralRegistersAsString() {
  return std::string(sizeof(zx_thread_state_general_regs_t) * 2, '0');
}

std::string GetRegsetAsString(Thread* thread, int regset) {
  FXL_DCHECK(regset == 0);
  if (!thread->registers()->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Unable to refresh general registers";
    return GetUninitializedGeneralRegistersAsString();
  }
  const zx_thread_state_general_regs_t* gregs =
    thread->registers()->GetGeneralRegisters();
  const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(gregs);
  return debugger_utils::EncodeByteArrayString(greg_bytes, sizeof(*gregs));
}

bool SetRegsetFromString(Thread* thread, int regset,
                         const fxl::StringView& value) {
  FXL_DCHECK(regset == 0);
  auto bytes = debugger_utils::DecodeByteArrayString(value);
  if (bytes.size() != sizeof(zx_thread_state_general_regs_t)) {
    FXL_LOG(ERROR) << "|value| doesn't match regset " << regset << " size of "
                   << bytes.size() << ": " << value;
    return false;
  }
  return SetRegsetHelper(thread, regset, bytes.data(), bytes.size());
}

std::string GetRegisterAsString(Thread* thread, int regno) {
  if (regno < 0 || regno >= kNumGeneralRegisters) {
    FXL_LOG(ERROR) << "Bad register number: " << regno;
    return "";
  }
  if (!thread->registers()->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Unable to refresh general registers";
    return std::string(sizeof(uint64_t) * 2, '0');
  }
  const zx_thread_state_general_regs_t* gregs =
    thread->registers()->GetGeneralRegisters();

  const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(gregs);
  greg_bytes += regno * sizeof(uint64_t);

  return debugger_utils::EncodeByteArrayString(greg_bytes, sizeof(uint64_t));
}

}  // namespace debugserver
