// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AUTOMATION_INSTRUCTION_EXECUTOR_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AUTOMATION_INSTRUCTION_EXECUTOR_H_

#include <cinttypes>
#include <map>
#include <vector>

#include "src/developer/debug/debug_agent/general_registers.h"
#include "src/developer/debug/debug_agent/process_handle.h"
#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

class AutomationInstructionExecutor {
 public:
  AutomationInstructionExecutor() = default;
  ~AutomationInstructionExecutor() = default;

  std::vector<debug_ipc::MemoryBlock> ExecuteInstructionVect(
      const std::vector<debug_ipc::AutomationInstruction>& instructions,
      const GeneralRegisters& regs, const ProcessHandle& handle);

  std::vector<debug_ipc::MemoryBlock> ExecuteLoopLoadMemory(
      const debug_ipc::AutomationInstruction& instr, const GeneralRegisters& regs,
      const ProcessHandle& handle);

  bool EvalConditionVect(const std::vector<debug_ipc::AutomationCondition>& conditions,
                         const GeneralRegisters& regs, const ProcessHandle& handle);

  bool EvalCondition(const debug_ipc::AutomationCondition& condition, const GeneralRegisters& regs,
                     const ProcessHandle& handle);

  uint64_t EvalOperand(const debug_ipc::AutomationOperand& operand, const GeneralRegisters& regs,
                       const ProcessHandle& handle);

  uint64_t EvalOperand(const debug_ipc::AutomationOperand& operand, const GeneralRegisters& regs,
                       const ProcessHandle& handle, const debug_ipc::MemoryBlock& loop_block,
                       uint64_t struct_base_pointer);

  std::map<uint32_t, uint64_t>& stored_values() { return stored_values_; }

  template <typename T>
  static T GetValueFromBytes(const std::vector<uint8_t>& bytes, size_t offset) {
    constexpr uint64_t kBitsPerByte = 8;
    T ret = 0;
    for (size_t i = 0; (i < sizeof(ret)) && (offset < bytes.size()); i++) {
      ret |= static_cast<uint64_t>(bytes[offset++]) << (i * kBitsPerByte);
    }
    return ret;
  }

 private:
  std::map<uint32_t, uint64_t> stored_values_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_AUTOMATION_INSTRUCTION_EXECUTOR_H_
