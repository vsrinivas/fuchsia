// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include "lib/fxl/logging.h"

namespace inferior_control {

int GetPCRegisterNumber() { return -1; }

int GetFPRegisterNumber() { return -1; }

int GetSPRegisterNumber() { return -1; }

namespace {

class RegistersDefault final : public Registers {
 public:
  RegistersDefault(Thread* thread) : Registers(thread) {}

  ~RegistersDefault() = default;

  bool IsSupported() override { return false; }

  bool RefreshRegset(int regset) override { return false; }

  bool WriteRegset(int regset) override { return false; }

  std::string GetRegsetAsString(int regset) override { return ""; }

  bool SetRegsetFromString(int regset, const fxl::StringView& value) override {
    return false;
  }

  std::string GetRegisterAsString(int regno) override { return ""; }

  bool GetRegister(int regno, void* buffer, size_t buf_size) override {
    return false;
  }

  bool SetRegister(int regno, const void* value, size_t value_size) override {
    return false;
  }

  bool SetSingleStep(bool enable) override { return false; }

  std::string GetFormattedRegset(int regset) override {
    return "unimplemented\n";
  }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(Thread* thread) {
  return std::unique_ptr<Registers>(new RegistersDefault(thread));
}

// static
std::string Registers::GetUninitializedGeneralRegistersAsString() { return ""; }

// static
size_t Registers::GetRegisterSize() { return 0; }

}  // namespace inferior_control
