// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>

#include <lib/fxl/arraysize.h>
#include <lib/fxl/logging.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "garnet/lib/debugger_utils/util.h"

#include "registers.h"

namespace inferior_control {

namespace {

class RegistersArm64 final : public Registers {
 public:
  static constexpr int kNumGeneralRegisters = 34;

  RegistersArm64(Thread* thread) : Registers(thread) {}

  bool GetRegister(int regno, void* buffer, size_t buf_size) override {
    if (regno < 0 || regno >= kNumGeneralRegisters) {
      FXL_LOG(ERROR) << "Bad register_number: " << regno;
      return false;
    }
    // On arm64 all general register values are 64-bit.
    // Note that this includes CPSR, whereas in the GDB RSP CPSR is 32 bits.
    if (buf_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Bad buffer size: " << buf_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<const uint8_t*>(&general_regs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(buffer, greg_bytes, buf_size);
    FXL_VLOG(2) << "Get register " << regno << " = "
                << debugger_utils::EncodeByteArrayString(greg_bytes, buf_size);
    return true;
  }

  bool SetRegister(int regno, const void* value, size_t value_size) override {
    if (regno < 0 || regno >= kNumGeneralRegisters) {
      FXL_LOG(ERROR) << "Invalid arm64 register number: " << regno;
      return false;
    }
    // On arm64 all general register values are 64-bit.
    // Note that this includes CPSR, whereas in the GDB RSP CPSR is 32 bits.
    if (value_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Invalid arm64 register value size: " << value_size;
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
    return general_regs_.pc;
  }

  zx_vaddr_t GetSP() override {
    return general_regs_.sp;
  }

  zx_vaddr_t GetFP() override {
    return general_regs_.r[29];
  }

  void SetPC(zx_vaddr_t pc) override {
    general_regs_.pc = pc;
  }

  bool SetSingleStep(bool enable) override {
    FXL_NOTIMPLEMENTED();
    return false;
  }

  std::string GetFormattedRegset(int regset) override {
    return "unimplemented\n";
  }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersArm64(thread));
}

}  // namespace inferior_control
