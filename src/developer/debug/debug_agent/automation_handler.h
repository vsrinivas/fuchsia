// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AUTOMATION_HANDLER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AUTOMATION_HANDLER_H_

#include <cinttypes>
#include <map>

#include "src/developer/debug/debug_agent/automation_instruction_executor.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/general_registers.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {

class AutomationHandler {
 public:
  void OnException(debug_ipc::NotifyException* exception, const GeneralRegisters& regs,
                   const ProcessHandle& handle, const std::map<uint32_t, Breakpoint>& breakpoints);

 private:
  AutomationInstructionExecutor executor;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AUTOMATION_HANDLER_H_
