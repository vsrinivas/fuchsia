// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/general_registers.h"

#include "src/developer/debug/debug_agent/arch.h"

namespace debug_agent {

void GeneralRegisters::CopyTo(std::vector<debug_ipc::Register>& dest) const {
  arch::SaveGeneralRegs(regs_, dest);
}

}  // namespace debug_agent
