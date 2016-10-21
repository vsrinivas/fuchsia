// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>

#include "util.h"

namespace debugserver {
namespace arch {
namespace {

class RegistersAmd64 final : public Registers {
 public:
  RegistersAmd64(const mx_handle_t thread_handle) : Registers(thread_handle) {}

  ~RegistersAmd64() = default;

  bool IsSupported() override { return true; }

  std::string GetGeneralRegisters() override {
    mx_x86_64_general_regs_t gregs;
    uint32_t gregs_size = sizeof(gregs);
    mx_status_t status = mx_thread_read_state(
        thread_handle(), MX_THREAD_STATE_REGSET0, &gregs, &gregs_size);
    if (status < 0) {
      util::LogErrorWithMxStatus("Failed to read x86_64 registers", status);
      return "";
    }

    FTL_DCHECK(gregs_size == sizeof(gregs));

    const size_t kResultSize = gregs_size * 2;
    std::unique_ptr<char[]> result(new char[kResultSize]);
    const uint8_t* greg_bytes = reinterpret_cast<const uint8_t*>(&gregs);

    for (size_t i = 0; i < kResultSize; i += 2) {
      util::EncodeByteString(*greg_bytes, result.get() + i);
      ++greg_bytes;
    }

    return std::string(result.release(), kResultSize);
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
