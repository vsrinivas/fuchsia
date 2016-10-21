// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <cstring>

#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>

#include "util.h"

namespace debugserver {
namespace arch {

int GetPCRegisterNumber() {
  return static_cast<int>(Amd64Register::RIP);
}

namespace {

bool GetGeneralRegistersHelper(const mx_handle_t thread_handle,
                               mx_x86_64_general_regs_t* out_gregs,
                               uint32_t* out_gregs_size) {
  FTL_DCHECK(out_gregs);
  FTL_DCHECK(out_gregs_size);

  *out_gregs_size = sizeof(*out_gregs);
  mx_status_t status = mx_thread_read_state(
      thread_handle, MX_THREAD_STATE_REGSET0, out_gregs, out_gregs_size);
  if (status < 0) {
    util::LogErrorWithMxStatus("Failed to read x86_64 registers", status);
    return false;
  }

  FTL_DCHECK(*out_gregs_size == sizeof(*out_gregs));

  return true;
}

class RegistersAmd64 final : public Registers {
 public:
  RegistersAmd64(const mx_handle_t thread_handle) : Registers(thread_handle) {}

  ~RegistersAmd64() = default;

  bool IsSupported() override { return true; }

  std::string GetGeneralRegisters() override {
    mx_x86_64_general_regs_t gregs;
    uint32_t gregs_size;
    if (!GetGeneralRegistersHelper(thread_handle(), &gregs, &gregs_size))
      return "";

    const size_t kResultSize = gregs_size * 2;
    std::unique_ptr<char[]> result(new char[kResultSize]);
    const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&gregs);

    for (size_t i = 0; i < kResultSize; i += 2) {
      util::EncodeByteString(*greg_bytes, result.get() + i);
      ++greg_bytes;
    }

    return std::string(result.release(), kResultSize);
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
    if (!GetGeneralRegistersHelper(thread_handle(), &gregs, &gregs_size))
      return false;

    std::memcpy(&gregs + register_number * sizeof(uint64_t), value, value_size);
    mx_status_t status = mx_thread_write_state(
        thread_handle(), MX_THREAD_STATE_REGSET0, &gregs, gregs_size);
    if (status < 0) {
      util::LogErrorWithMxStatus("Failed to write x86_64 registers", status);
      return false;
    }

    return true;
  }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(const mx_handle_t thread_handle) {
  return std::unique_ptr<Registers>(new RegistersAmd64(thread_handle));
}

// static
std::string Registers::GetUninitializedGeneralRegisters() {
  return std::string(sizeof(mx_x86_64_general_regs_t) * 2, '0');
}

}  // namespace arch
}  // namespace debugserver
