// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers_arm64.h"
#include "registers.h"

#include <cinttypes>
#include <cstring>

#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>

#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"

#include "garnet/lib/debugger_utils/util.h"

//#include "arch-arm64.h"
//#include "thread.h"

namespace inferior_control {

int GetPCRegisterNumber() { return static_cast<int>(Arm64Register::PC); }

int GetFPRegisterNumber() { return static_cast<int>(Arm64Register::FP); }

int GetSPRegisterNumber() { return static_cast<int>(Arm64Register::SP); }

namespace {

// In the GDB RSP, cpsr is 32 bits, which throws a wrench into the works.

struct RspArm64GeneralRegs {
  // same as zx_thread_state_general_regs except cpsr is 32 bits
  uint64_t r[30];
  uint64_t lr;
  uint64_t sp;
  uint64_t pc;
  uint32_t cpsr;
} __PACKED;

class RegistersArm64 final : public Registers {
 public:
  RegistersArm64(Thread* thread) : Registers(thread) {
    memset(&gregs_, 0, sizeof(gregs_));
  }

  ~RegistersArm64() = default;

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
    RspArm64GeneralRegs rsp_gregs;
    TranslateToRsp(&rsp_gregs);
    const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&rsp_gregs);
    return debugger_utils::EncodeByteArrayString(greg_bytes, sizeof(rsp_gregs));
  }

  bool SetRegsetFromString(int regset, const fxl::StringView& value) override {
    FXL_DCHECK(regset == 0);
    RspArm64GeneralRegs rsp_gregs;
    if (!SetRegsetFromStringHelper(regset, &rsp_gregs, sizeof(rsp_gregs),
                                   value))
      return false;
    TranslateFromRsp(&rsp_gregs);
    return true;
  }

  std::string GetRegisterAsString(int regno) override {
    if (regno < 0 || regno >= static_cast<int>(Arm64Register::NUM_REGISTERS)) {
      FXL_LOG(ERROR) << "Bad register number: " << regno;
      return "";
    }

    const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&gregs_);
    greg_bytes += regno * sizeof(uint64_t);

    size_t data_size = sizeof(uint64_t);
    if (regno == static_cast<int>(Arm64Register::CPSR))
      data_size = sizeof(uint32_t);

    return debugger_utils::EncodeByteArrayString(greg_bytes, data_size);
  }

  bool GetRegister(int regno, void* buffer, size_t buf_size) override {
    if (regno < 0 || regno >= static_cast<int>(Arm64Register::NUM_REGISTERS)) {
      FXL_LOG(ERROR) << "Bad register_number: " << regno;
      return false;
    }
    // On arm64 all general register values are 64-bit.
    // Note that this includes CPSR, whereas in the GDB RSP CPSR is 32 bits.
    if (buf_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Bad buffer size: " << buf_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<const uint8_t*>(&gregs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(buffer, greg_bytes, buf_size);
    FXL_VLOG(1) << "Get register " << regno << " = "
                << debugger_utils::EncodeByteArrayString(greg_bytes, buf_size);
    return true;
  }

  bool SetRegister(int regno, const void* value, size_t value_size) override {
    if (regno < 0 || regno >= static_cast<int>(Arm64Register::NUM_REGISTERS)) {
      FXL_LOG(ERROR) << "Invalid arm64 register number: " << regno;
      return false;
    }
    // On arm64 all general register values are 64-bit.
    // Note that this includes CPSR, whereas in the GDB RSP CPSR is 32 bits.
    if (value_size != sizeof(uint64_t)) {
      FXL_LOG(ERROR) << "Invalid arm64 register value size: " << value_size;
      return false;
    }

    auto greg_bytes = reinterpret_cast<uint8_t*>(&gregs_);
    greg_bytes += regno * sizeof(uint64_t);
    std::memcpy(greg_bytes, value, value_size);
    FXL_VLOG(1) << "Set register " << regno << " = "
                << debugger_utils::EncodeByteArrayString(greg_bytes,
                                                         value_size);
    return true;
  }

  bool SetSingleStep(bool enable) override {
    FXL_NOTIMPLEMENTED();
    return false;
  }

  std::string GetFormattedRegset(int regset) override {
    return "unimplemented\n";
  }

 private:
  void TranslateToRsp(RspArm64GeneralRegs* rsp_gregs) {
    static_assert(arraysize(rsp_gregs->r) == arraysize(gregs_.r),
                  "gregs_.r size");
    memcpy(&rsp_gregs->r[0], &gregs_.r[0], arraysize(gregs_.r));
    rsp_gregs->lr = gregs_.lr;
    rsp_gregs->sp = gregs_.sp;
    rsp_gregs->pc = gregs_.pc;
    rsp_gregs->cpsr = gregs_.cpsr;
  }

  void TranslateFromRsp(const RspArm64GeneralRegs* rsp_gregs) {
    static_assert(arraysize(rsp_gregs->r) == arraysize(gregs_.r),
                  "gregs_.r size");
    memcpy(&gregs_.r[0], &rsp_gregs->r[0], arraysize(gregs_.r));
    gregs_.lr = rsp_gregs->lr;
    gregs_.sp = rsp_gregs->sp;
    gregs_.pc = rsp_gregs->pc;
    gregs_.cpsr = rsp_gregs->cpsr;
  }

  zx_thread_state_general_regs gregs_;
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersArm64(thread));
}

// static
std::string Registers::GetUninitializedGeneralRegistersAsString() {
  return std::string(sizeof(RspArm64GeneralRegs) * 2, '0');
}

// static
size_t Registers::GetRegisterSize() { return sizeof(uint64_t); }

}  // namespace inferior_control
