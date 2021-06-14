// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/automation_handler.h"

namespace debug_agent {

void AutomationHandler::OnException(debug_ipc::NotifyException* exception,
                                    const GeneralRegisters& regs, const ProcessHandle& handle,
                                    const std::map<uint32_t, Breakpoint>& breakpoints) {
  const std::vector<debug_ipc::AutomationInstruction>* full_instruction_vector = nullptr;

  for (debug_ipc::BreakpointStats cur_breakpoint : exception->hit_breakpoints) {
    auto cur_breakpoint_iter = breakpoints.find(cur_breakpoint.id);
    if (cur_breakpoint_iter != breakpoints.end() &&
        cur_breakpoint_iter->second.settings().has_automation) {
      if (full_instruction_vector != nullptr) {
        DEBUG_LOG(Thread) << "Skipping automatic memory collection due to hitting multiple "
                             "automated breakpoints at the same time.";
        return;
      }
      full_instruction_vector = &cur_breakpoint_iter->second.settings().instructions;
    }
  }
  if (full_instruction_vector != nullptr) {
    auto auto_blocks = executor.ExecuteInstructionVect(*full_instruction_vector, regs, handle);
    exception->memory_blocks.insert(exception->memory_blocks.begin(), auto_blocks.begin(),
                                    auto_blocks.end());
  }
}

}  // namespace debug_agent
