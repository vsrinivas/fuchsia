// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers_amd64.h"
#include "registers.h"

#include <cinttypes>
#include <cstring>

#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

#include "arch_x86.h"
#include "thread.h"

namespace debugserver {

int GetPCRegisterNumber() { return static_cast<int>(Amd64Register::RIP); }

int GetFPRegisterNumber() { return static_cast<int>(Amd64Register::RBP); }

int GetSPRegisterNumber() { return static_cast<int>(Amd64Register::RSP); }

namespace {

// Includes all registers if |register_number| is -1.
// TODO: Here as elsewhere: more regsets.
std::string GetRegisterAsStringHelper(const zx_thread_state_general_regs& gregs,
                                      int regno) {
  FXL_DCHECK(regno >= -1 &&
             regno < static_cast<int>(Amd64Register::NUM_REGISTERS));

  // Based on the value of |regno|, we either need to fit in all
  // registers or just a single one.
  const size_t kDataSize = regno < 0 ? sizeof(gregs) : sizeof(uint64_t);
  const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&gregs);

  greg_bytes += regno < 0 ? 0 : regno * sizeof(uint64_t);

  return EncodeByteArrayString(greg_bytes, kDataSize);
}

class RegistersAmd64 final : public Registers {
 public:
  RegistersAmd64(Thread* thread) : Registers(thread) {
    memset(&gregs_, 0, sizeof(gregs_));
  }

  ~RegistersAmd64() = default;

  bool IsSupported() override { return true; }

  bool RefreshRegset(int regset) override {
    FXL_DCHECK(regset == 0);
    return RefreshRegsetHelper(regset, &gregs_, sizeof(gregs_));
  }

  bool WriteRegset(int regset) override {
    FXL_DCHECK(regset == 0);
    return WriteRegsetHelper(regset, &gregs_, sizeof(gregs_));
  }

  std::string GetRegsetAsString(int regset) override {
    FXL_DCHECK(regset == 0);
    return GetRegisterAsStringHelper(gregs_, -1);
  }

  bool SetRegsetFromString(int regset, const fxl::StringView& value) override {
    FXL_DCHECK(regset == 0);
    return SetRegsetFromStringHelper(regset, &gregs_, sizeof(gregs_), value);
  }

  std::string GetRegisterAsString(int regno) override {
    if (regno < 0 || regno >= static_cast<int>(Amd64Register::NUM_REGISTERS)) {
      FXL_LOG(ERROR) << "Bad register number: " << regno;
      return "";
    }

    return GetRegisterAsStringHelper(gregs_, regno);
  }

  bool GetRegister(int regno, void* buffer, size_t buf_size) override {
    if (regno < 0 || regno >= static_cast<int>(Amd64Register::NUM_REGISTERS)) {
      FXL_LOG(ERROR) << "Bad register_number: " << regno;
      return false;
    }
    if (buf_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Bad buffer size: " << buf_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<const uint8_t*>(&gregs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(buffer, greg_bytes, buf_size);
    FXL_VLOG(1) << "Get register " << regno << " = "
                << EncodeByteArrayString(greg_bytes, buf_size);
    return true;
  }

  bool SetRegister(int regno, const void* value, size_t value_size) override {
    if (regno < 0 || regno >= static_cast<int>(Amd64Register::NUM_REGISTERS)) {
      FXL_LOG(ERROR) << "Invalid x86_64 register number: " << regno;
      return false;
    }
    // On x86_64 all general register values are 64-bit.
    if (value_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Invalid x86_64 register value size: " << value_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<uint8_t*>(&gregs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(greg_bytes, value, value_size);
    FXL_VLOG(1) << "Set register " << regno << " = "
                << EncodeByteArrayString(greg_bytes, value_size);
    return true;
  }

  bool SetSingleStep(bool enable) override {
    if (enable)
      gregs_.rflags |= X86_EFLAGS_TF_MASK;
    else
      gregs_.rflags &= ~static_cast<uint64_t>(X86_EFLAGS_TF_MASK);
    FXL_VLOG(2) << "rflags.TF set to " << enable;
    return true;
  }

  std::string GetFormattedRegset(int regset) override {
    if (regset != 0) return fxl::StringPrintf("Invalid regset %d\n", regset);

    return FormatGeneralRegisters();
  }

 private:
  std::string FormatGeneralRegisters() {
    std::string result;

    result += fxl::StringPrintf("  CS: %#18llx RIP: %#18" PRIx64
                                " EFL: %#18" PRIx64 "\n",
                                0ull, gregs_.rip, gregs_.rflags);
    result += fxl::StringPrintf(" RAX: %#18" PRIx64 " RBX: %#18" PRIx64
                                " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
                                gregs_.rax, gregs_.rbx, gregs_.rcx, gregs_.rdx);
    result += fxl::StringPrintf(" RSI: %#18" PRIx64 " RDI: %#18" PRIx64
                                " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
                                gregs_.rsi, gregs_.rdi, gregs_.rbp, gregs_.rsp);
    result += fxl::StringPrintf("  R8: %#18" PRIx64 "  R9: %#18" PRIx64
                                " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
                                gregs_.r8, gregs_.r9, gregs_.r10, gregs_.r11);
    result += fxl::StringPrintf(" R12: %#18" PRIx64 " R13: %#18" PRIx64
                                " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
                                gregs_.r12, gregs_.r13, gregs_.r14, gregs_.r15);

    return result;
  }

  zx_thread_state_general_regs gregs_;
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersAmd64(thread));
}

// static
std::string Registers::GetUninitializedGeneralRegistersAsString() {
  return std::string(sizeof(zx_thread_state_general_regs) * 2, '0');
}

// static
size_t Registers::GetRegisterSize() { return sizeof(uint64_t); }

}  // namespace debugserver
