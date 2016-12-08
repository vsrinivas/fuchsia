// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include "lib/ftl/logging.h"

namespace debugserver {
namespace arch {

int GetPCRegisterNumber() {
  FTL_NOTIMPLEMENTED();
  return -1;
}

int GetFPRegisterNumber() {
  FTL_NOTIMPLEMENTED();
  return -1;
}

int GetSPRegisterNumber() {
  FTL_NOTIMPLEMENTED();
  return -1;
}

namespace {

class RegistersDefault final : public Registers {
 public:
  RegistersDefault(Thread* thread) : Registers(thread) {}

  ~RegistersDefault() = default;

  bool IsSupported() override { return false; }

  bool RefreshGeneralRegisters() override {
    FTL_NOTIMPLEMENTED();
    return false;
  }

  std::string GetGeneralRegisters() override {
    FTL_NOTIMPLEMENTED();
    return "";
  }

  bool SetGeneralRegisters(const ftl::StringView& value) override {
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
    FTL_NOTIMPLEMENTED();
    return false;
  }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersDefault(thread));
}

// static
std::string Registers::GetUninitializedGeneralRegisters() {
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
