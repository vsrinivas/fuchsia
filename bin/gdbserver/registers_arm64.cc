// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

namespace debugserver {
namespace arch {

int GetPCRegisterNumber() {
  // TODO(armansito): Implement
  return -1;
}

namespace {

class RegistersArm64 final : public Registers {
 public:
  RegistersArm64(const mx_handle_t thread_handle)
      : Registers(thread_handle) {}

  ~RegistersArm64() = default;

  bool IsSupported() override {
    // TODO(armansito): Implement.
    return false;
  }

  std::string GetGeneralRegisters() override {
    // TODO(armansito): Implement.
    return "";
  }

  bool SetRegisterValue(int register_number,
                        void* value,
                        size_t value_size) override {
    // TODO(armansito): Implement.
    return false;
  }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(const mx_handle_t thread_handle) {
  return std::unique_ptr<Registers>(new RegistersArm64(thread_handle));
}

// static
std::string Registers::GetUninitializedGeneralRegisters() {
  // TODO(armansito): Implement.
  return "";
}

}  // namespace arch
}  // namespace debugserver
