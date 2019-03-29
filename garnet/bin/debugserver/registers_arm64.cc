// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/arraysize.h>
#include <zircon/syscalls/debug.h>

#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/inferior_control/thread.h"

#include "registers.h"

namespace debugserver {

constexpr int kFpRegisterNumber = 29;
constexpr int kSpRegisterNumber = 31;
constexpr int kPcRegisterNumber = 32;
constexpr int kCpsrRegisterNumber = 33;
constexpr int kNumGeneralRegisters = 34;

int GetFPRegisterNumber() { return kFpRegisterNumber; }

int GetSPRegisterNumber() { return kSpRegisterNumber; }

int GetPCRegisterNumber() { return kPcRegisterNumber; }

// In the GDB RSP, cpsr is 32 bits, which throws a wrench into the works.

struct RspArm64GeneralRegs {
  // same as zx_thread_state_general_regs_t except cpsr is 32 bits
  uint64_t r[30];
  uint64_t lr;
  uint64_t sp;
  uint64_t pc;
  uint32_t cpsr;
} __PACKED;

std::string GetUninitializedGeneralRegistersAsString() {
  return std::string(sizeof(RspArm64GeneralRegs) * 2, '0');
}

static void TranslateToRsp(const zx_thread_state_general_regs_t* gregs,
                           RspArm64GeneralRegs* out_rsp_gregs) {
  static_assert(arraysize(out_rsp_gregs->r) == arraysize(gregs->r),
                "gregs_.r size");
  memcpy(&out_rsp_gregs->r[0], &gregs->r[0], arraysize(gregs->r));
  out_rsp_gregs->lr = gregs->lr;
  out_rsp_gregs->sp = gregs->sp;
  out_rsp_gregs->pc = gregs->pc;
  out_rsp_gregs->cpsr = gregs->cpsr;
}

static void TranslateFromRsp(const RspArm64GeneralRegs* rsp_gregs,
                             zx_thread_state_general_regs_t* out_gregs) {
  static_assert(arraysize(rsp_gregs->r) == arraysize(out_gregs->r),
                "gregs->r size");
  memcpy(&out_gregs->r[0], &rsp_gregs->r[0], arraysize(out_gregs->r));
  out_gregs->lr = rsp_gregs->lr;
  out_gregs->sp = rsp_gregs->sp;
  out_gregs->pc = rsp_gregs->pc;
  out_gregs->cpsr = rsp_gregs->cpsr;
}

std::string GetRegsetAsString(Thread* thread, int regset) {
  FXL_DCHECK(regset == 0);
  if (!thread->registers()->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Unable to refresh general registers";
    return GetUninitializedGeneralRegistersAsString();
  }
  const zx_thread_state_general_regs_t* gregs =
    thread->registers()->GetGeneralRegisters();
  RspArm64GeneralRegs rsp_gregs;
  TranslateToRsp(gregs, &rsp_gregs);
  const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&rsp_gregs);
  return debugger_utils::EncodeByteArrayString(greg_bytes, sizeof(rsp_gregs));
}

bool SetRegsetFromString(Thread* thread, int regset,
                         const fxl::StringView& value) {
  FXL_DCHECK(regset == 0);
  RspArm64GeneralRegs rsp_gregs;
  auto bytes = debugger_utils::DecodeByteArrayString(value);
  if (bytes.size() != sizeof(RspArm64GeneralRegs)) {
    FXL_LOG(ERROR) << "|value| doesn't match regset " << regset << " size of "
                   << bytes.size() << ": " << value;
    return false;
  }
  memcpy(&rsp_gregs, bytes.data(), bytes.size());
  zx_thread_state_general_regs_t gregs;
  TranslateFromRsp(&rsp_gregs, &gregs);
  return SetRegsetHelper(thread, regset, &gregs, sizeof(gregs));
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

  size_t data_size = sizeof(uint64_t);
  if (regno == kCpsrRegisterNumber) {
    data_size = sizeof(uint32_t);
  }

  return debugger_utils::EncodeByteArrayString(greg_bytes, data_size);
}

}  // namespace debugserver
