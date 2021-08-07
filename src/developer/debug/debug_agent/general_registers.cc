// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/general_registers.h"

#include "src/developer/debug/debug_agent/arch.h"

namespace debug_agent {

void GeneralRegisters::CopyTo(std::vector<debug_ipc::Register>& dest) const {
  arch::SaveGeneralRegs(regs_, dest);
}

std::optional<uint64_t> GeneralRegisters::GetRegister(const debug::RegisterID reg_id) const {
  std::vector<debug_ipc::Register> reg_vect;
  CopyTo(reg_vect);
  for (const auto& reg : reg_vect) {
    if (reg.id == reg_id) {
      return std::optional<uint64_t>(static_cast<uint64_t>(reg.GetValue()));
    }
  }
  return std::nullopt;
}

}  // namespace debug_agent
