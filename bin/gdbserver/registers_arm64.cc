// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include "lib/ftl/logging.h"

namespace debugserver {
namespace arch {

int GetPCRegisterNumber() {
  // TODO(armansito): Implement
  FTL_NOTIMPLEMENTED();
  return -1;
}

int GetFPRegisterNumber() {
  // TODO(armansito): Implement
  FTL_NOTIMPLEMENTED();
  return -1;
}

int GetSPRegisterNumber() {
  // TODO(armansito): Implement
  FTL_NOTIMPLEMENTED();
  return -1;
}

namespace {

class RegistersArm64 final : public Registers {
 public:
  RegistersArm64(Thread* thread) : Registers(thread) {}

  ~RegistersArm64() = default;

  bool IsSupported() override {
    // TODO(armansito): Implement.
    FTL_NOTIMPLEMENTED();
    return false;
  }

  bool RefreshGeneralRegisters() override {
    // TODO(armansito): Implement.
    FTL_NOTIMPLEMENTED();
    return false;
  }

  std::string GetGeneralRegisters() override {
    // TODO(armansito): Implement.
    FTL_NOTIMPLEMENTED();
    return "";
  }

  bool SetGeneralRegisters(const ftl::StringView& value) override {
    // TODO(armansito): Implement.
    FTL_NOTIMPLEMENTED();
    return false;
  }

  std::string GetRegisterValue(unsigned int register_number) override {
    FTL_NOTIMPLEMENTED();
    return "";
  }

  bool SetRegisterValue(int register_number,
                        void* value,
                        size_t value_size) override {
    // TODO(armansito): Implement.
    FTL_NOTIMPLEMENTED();
    return false;
  }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersArm64(thread));
}

// static
std::string Registers::GetUninitializedGeneralRegisters() {
  // TODO(armansito): Implement.
  FTL_NOTIMPLEMENTED();
  return "";
}

// static
size_t Registers::GetRegisterSize() {
  FTL_NOTIMPLEMENTED();
  return 0;
}

}  // namespace arch
}  // namespace debugserver
