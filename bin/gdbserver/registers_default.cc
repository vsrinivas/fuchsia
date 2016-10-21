// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

namespace debugserver {
namespace arch {
namespace {

class RegistersDefault final : public Registers {
 public:
  RegistersDefault(const mx_handle_t thread_handle)
      : Registers(thread_handle) {}

  ~RegistersDefault() = default;

  bool IsSupported() override { return false; }

  std::string GetGeneralRegisters() override { return ""; }
};

}  // namespace

// static
std::unique_ptr<Registers> Registers::Create(const mx_handle_t thread_handle) {
  return std::unique_ptr<Registers>(new RegistersDefault(thread_handle));
}

// static
std::string Registers::GetUninitializedGeneralRegisters() {
  return "";
}

}  // namespace arch
}  // namespace debugserver
