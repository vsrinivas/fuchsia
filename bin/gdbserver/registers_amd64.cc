// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <cstring>

#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>

#include "lib/ftl/logging.h"

#include "thread.h"
#include "util.h"

namespace debugserver {
namespace arch {

int GetPCRegisterNumber() {
  return static_cast<int>(Amd64Register::RIP);
}

int GetFPRegisterNumber() {
  return static_cast<int>(Amd64Register::RBP);
}

int GetSPRegisterNumber() {
  return static_cast<int>(Amd64Register::RSP);
}

int ComputeGdbSignal(const mx_exception_context_t& exception_context) {
  int sigval;
  uint64_t arch_exception = exception_context.arch.u.x86_64.vector;

  switch (arch_exception) {
    case 0:  // Divide Error (division by zero)
      sigval = 8;
      break;
    case 1:  // Debug Exceptions
      sigval = 5;
      break;
    case 2:  // NMI
      sigval = 29;
      break;
    case 3:  // Breakpoint
      sigval = 5;
      break;
    case 4:  // Overflow
      sigval = 8;
      break;
    case 5:  // Bound Range Exceeded
      sigval = 11;
      break;
    case 6:  // Invalid Opcode
      sigval = 4;
      break;
    case 7:  // Coprocessor Not Available
      sigval = 8;
      break;
    case 8:  // Double Fault
      sigval = 7;
      break;
    case 9:   // Coprocessor Segment Overrun (i386 or earlier only)
    case 10:  // Invalid Task State Segment
    case 11:  // Segment Not Present
    case 12:  // Stack Fault
    case 13:  // General Protection Fault
    case 14:  // Page Fault
      sigval = 11;
      break;
    case 15:  // Reserved (-> SIGUSR1)
      sigval = 10;
      break;
    case 16:  // Math Fault
    case 17:  // Aligment Check
      sigval = 7;
      break;
    case 18:  // Machine check (-> SIGURG)
      sigval = 23;
      break;
    case 19:  // SIMD Floating-Point Exception
      sigval = 8;
      break;
    case 20:  // Virtualization Exception (-> SIGVTALRM)
      sigval = 26;
      break;
    case 21:  // Control Protection Exception
      sigval = 11;
      break;
    case 22 ... 31:
      sigval = 10;  // reserved (-> SIGUSR1 for now)
      break;
    default:
      sigval = 12;  // "software generated" (-> SIGUSR2 for now)
      break;
  }

  FTL_VLOG(1) << "x86 (AMD64) exception (" << arch_exception
              << ") mapped to: " << sigval;

  return sigval;
}

namespace {

bool GetGeneralRegistersHelper(const mx_handle_t thread_handle,
                               mx_x86_64_general_regs_t* out_gregs,
                               uint32_t* out_gregs_size) {
  FTL_DCHECK(out_gregs);
  FTL_DCHECK(out_gregs_size);

  *out_gregs_size = sizeof(*out_gregs);
  mx_status_t status = mx_thread_read_state(
      thread_handle, MX_THREAD_STATE_REGSET0, out_gregs, sizeof(*out_gregs),
      out_gregs_size);
  if (status < 0) {
    util::LogErrorWithMxStatus("Failed to read x86_64 registers", status);
    return false;
  }

  FTL_DCHECK(*out_gregs_size == sizeof(*out_gregs));

  return true;
}

// Includes all registers if |register_number| is -1.
std::string GetRegisterValueAsString(const mx_x86_64_general_regs_t& gregs,
                                     int register_number) {
  FTL_DCHECK(register_number < static_cast<int>(Amd64Register::NUM_REGISTERS));

  // Based on the value of |register_number|, we either need to fit in all
  // registers or just a single one.
  const size_t kDataSize =
      register_number < 0 ? sizeof(gregs) : sizeof(uint64_t);
  const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&gregs);

  // TODO(armansito): Not all x86-64 registers store 64-bit values; the main
  // ones all do but lower-bits of all the "r*" registers can be interpreted as
  // 32-bit, 16-bit, or 8-bit registers (e.g. rax vs eax, ax, al, etc). So using
  // |register_number| here to index into |gregs| isn't really a good idea. But
  // we're doing it for now.
  greg_bytes += register_number < 0 ? 0 : register_number * sizeof(uint64_t);

  return util::EncodeByteArrayString(greg_bytes, kDataSize);
}

class RegistersAmd64 final : public Registers {
 public:
  RegistersAmd64(Thread* thread) : Registers(thread) {
    memset(&gregs_, 0, sizeof(gregs_));
  }

  ~RegistersAmd64() = default;

  bool IsSupported() override { return true; }

  bool RefreshGeneralRegisters() override {
    // We report all zeros for the registers if the thread was just created.
    if (thread()->state() == Thread::State::kNew) {
      memset(&gregs_, 0, sizeof(gregs_));
      return true;
    }

    uint32_t gregs_size;
    return GetGeneralRegistersHelper(thread()->debug_handle(), &gregs_,
                                     &gregs_size);
  }

  std::string GetGeneralRegisters() override {
    if (!RefreshGeneralRegisters())
      return "";

    return GetRegisterValueAsString(gregs_, -1);
  }

  bool SetGeneralRegisters(const ftl::StringView& value) override {
    auto bytes = util::DecodeByteArrayString(value);
    if (bytes.size() != sizeof(mx_x86_64_general_regs_t)) {
      FTL_LOG(ERROR) << "|value| doesn't match x86-64 general registers size: "
                     << value;
      return false;
    }

    mx_x86_64_general_regs_t* gregs =
        reinterpret_cast<mx_x86_64_general_regs_t*>(bytes.data());
    mx_status_t status =
        mx_thread_write_state(thread()->debug_handle(), MX_THREAD_STATE_REGSET0,
                              gregs, sizeof(*gregs));
    if (status < 0) {
      util::LogErrorWithMxStatus("Failed to write x86_64 registers", status);
      return false;
    }

    FTL_VLOG(1) << "General registers written: " << bytes.size() << " bytes";

    return true;
  }

  std::string GetRegisterValue(unsigned int register_number) override {
    if (register_number >=
        static_cast<unsigned int>(Amd64Register::NUM_REGISTERS)) {
      FTL_LOG(ERROR) << "Bad register_number: " << register_number;
      return "";
    }

    return GetRegisterValueAsString(gregs_, register_number);
  }

  bool SetRegisterValue(int register_number,
                        void* value,
                        size_t value_size) override {
    if (register_number >= static_cast<int>(Amd64Register::NUM_REGISTERS) ||
        register_number < 0) {
      FTL_LOG(ERROR) << "Invalid x86_64 register number: " << register_number;
      return false;
    }

    // On x86_64 all register values are 64-bit.
    if (value_size != sizeof(uint64_t)) {
      FTL_LOG(ERROR) << "Invalid x86_64 register value size: " << value_size;
      return false;
    }

    mx_x86_64_general_regs_t gregs;
    uint32_t gregs_size;
    if (!GetGeneralRegistersHelper(thread()->debug_handle(), &gregs,
                                   &gregs_size))
      return false;

    std::memcpy(&gregs + register_number * sizeof(uint64_t), value, value_size);
    mx_status_t status = mx_thread_write_state(
        thread()->debug_handle(), MX_THREAD_STATE_REGSET0, &gregs, gregs_size);
    if (status < 0) {
      util::LogErrorWithMxStatus("Failed to write x86_64 registers", status);
      return false;
    }

    return true;
  }

 private:
  mx_x86_64_general_regs_t gregs_;
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersAmd64(thread));
}

// static
std::string Registers::GetUninitializedGeneralRegisters() {
  return std::string(sizeof(mx_x86_64_general_regs_t) * 2, '0');
}

// static
size_t Registers::GetRegisterSize() {
  return sizeof(uint64_t);
}

}  // namespace arch
}  // namespace debugserver
