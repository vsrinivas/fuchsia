// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>
#include <cstring>

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "garnet/lib/debugger_utils/util.h"

#include "registers.h"
#include "arch_x64.h"
#include "thread.h"

namespace inferior_control {

namespace {

class RegistersX64 final : public Registers {
 public:
  static constexpr int kNumGeneralRegisters = 18;

  RegistersX64(Thread* thread) : Registers(thread) {}

  bool GetRegister(int regno, void* buffer, size_t buf_size) override {
    if (regno < 0 || regno >= kNumGeneralRegisters) {
      FXL_LOG(ERROR) << "Bad register_number: " << regno;
      return false;
    }
    if (buf_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Bad buffer size: " << buf_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<const uint8_t*>(&general_regs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(buffer, greg_bytes, buf_size);
    FXL_VLOG(2) << "Get register " << regno << " = (raw) "
                << debugger_utils::EncodeByteArrayString(greg_bytes, buf_size);
    return true;
  }

  bool SetRegister(int regno, const void* value, size_t value_size) override {
    if (regno < 0 || regno >= kNumGeneralRegisters) {
      FXL_LOG(ERROR) << "Invalid X64 register number: " << regno;
      return false;
    }
    // On X64 all general register values are 64-bit.
    if (value_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Invalid X64 register value size: " << value_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<uint8_t*>(&general_regs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(greg_bytes, value, value_size);
    FXL_VLOG(2) << "Set register " << regno << " = "
                << debugger_utils::EncodeByteArrayString(greg_bytes,
                                                         value_size);
    return true;
  }

  zx_vaddr_t GetPC() override {
    return general_regs_.rip;
  }

  zx_vaddr_t GetSP() override {
    return general_regs_.rsp;
  }

  zx_vaddr_t GetFP() override {
    return general_regs_.rbp;
  }

  void SetPC(zx_vaddr_t pc) override {
    general_regs_.rip = pc;
  }

  bool SetSingleStep(bool enable) override {
    if (enable)
      general_regs_.rflags |= X86_EFLAGS_TF_MASK;
    else
      general_regs_.rflags &= ~static_cast<uint64_t>(X86_EFLAGS_TF_MASK);
    FXL_VLOG(4) << "rflags.TF set to " << enable;
    return true;
  }

  std::string GetFormattedRegset(int regset) override {
    if (regset != 0)
      return fxl::StringPrintf("Invalid regset %d\n", regset);

    return FormatGeneralRegisters();
  }

 private:
  std::string FormatGeneralRegisters() {
    std::string result;
    const zx_thread_state_general_regs_t* gr = &general_regs_;

    result += fxl::StringPrintf("  CS: %#18llx RIP: %#18" PRIx64
                                " EFL: %#18" PRIx64 "\n",
                                0ull, gr->rip, gr->rflags);
    result += fxl::StringPrintf(" RAX: %#18" PRIx64 " RBX: %#18" PRIx64
                                " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
                                gr->rax, gr->rbx, gr->rcx, gr->rdx);
    result += fxl::StringPrintf(" RSI: %#18" PRIx64 " RDI: %#18" PRIx64
                                " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
                                gr->rsi, gr->rdi, gr->rbp, gr->rsp);
    result += fxl::StringPrintf("  R8: %#18" PRIx64 "  R9: %#18" PRIx64
                                " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
                                gr->r8, gr->r9, gr->r10, gr->r11);
    result += fxl::StringPrintf(" R12: %#18" PRIx64 " R13: %#18" PRIx64
                                " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
                                gr->r12, gr->r13, gr->r14, gr->r15);

    return result;
  }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersX64(thread));
}

}  // namespace inferior_control
