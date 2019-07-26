// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <ddk/device.h>
#include <zircon/types.h>

namespace hisi_clock {

enum class RegisterBank {
  Sctrl,
  Peri,
};

class Gate {
 public:
  constexpr Gate(const uint32_t reg, const uint32_t bit, const RegisterBank bank)
      : reg_(reg), bit_(bit), bank_(bank) {}
  ~Gate() = default;

  uint32_t Reg() const { return reg_; }
  uint32_t Bit() const { return bit_; }
  RegisterBank Bank() const { return bank_; }

 private:
  const uint32_t reg_;
  const uint32_t bit_;
  const RegisterBank bank_;
};

}  // namespace hisi_clock
