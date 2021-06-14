// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/automation_instruction_executor.h"

namespace debug_agent {

std::vector<debug_ipc::MemoryBlock> AutomationInstructionExecutor::ExecuteInstructionVect(
    const std::vector<debug_ipc::AutomationInstruction>& instructions, const GeneralRegisters& regs,
    const ProcessHandle& handle) {
  std::vector<debug_ipc::MemoryBlock> out_block_vect;
  for (auto instr : instructions) {
    if (EvalConditionVect(instr.conditions(), regs, handle)) {
      switch (instr.kind()) {
        case debug_ipc::AutomationInstructionKind::kNop:
          break;
        case debug_ipc::AutomationInstructionKind::kLoadMemory: {
          std::vector<debug_ipc::MemoryBlock> temp_block_vect =
              handle.ReadMemoryBlocks(EvalOperand(instr.address(), regs, handle),
                                      EvalOperand(instr.length(), regs, handle));
          out_block_vect.insert(out_block_vect.begin(), temp_block_vect.begin(),
                                temp_block_vect.end());
          break;
        }
        case debug_ipc::AutomationInstructionKind::kLoopLoadMemory: {
          std::vector<debug_ipc::MemoryBlock> temp_block_vect =
              ExecuteLoopLoadMemory(instr, regs, handle);
          out_block_vect.insert(out_block_vect.begin(), temp_block_vect.begin(),
                                temp_block_vect.end());
          break;
        }
        case debug_ipc::AutomationInstructionKind::kComputeAndStore:
          stored_values_.emplace(instr.slot_index(),
                                 EvalOperand(instr.store_value(), regs, handle));
          break;
        case debug_ipc::AutomationInstructionKind::kClearStoredValues:
          stored_values_.clear();
          break;
      }
    }
  }
  return out_block_vect;
}

std::vector<debug_ipc::MemoryBlock> AutomationInstructionExecutor::ExecuteLoopLoadMemory(
    const debug_ipc::AutomationInstruction& instr, const GeneralRegisters& regs,
    const ProcessHandle& handle) {
  std::vector<debug_ipc::MemoryBlock> out_block_vect;
  uint64_t address_val = EvalOperand(instr.address(), regs, handle);
  uint64_t length_val = EvalOperand(instr.length(), regs, handle);

  std::vector<debug_ipc::MemoryBlock> struct_array_vect =
      handle.ReadMemoryBlocks(address_val, length_val * instr.item_size());

  if (!struct_array_vect[0].valid) {
    return out_block_vect;
  }

  for (uint32_t i = 0; i < length_val; i++) {
    uint64_t cur_struct_base = instr.item_size() * i;
    uint64_t address_from_struct = EvalOperand(instr.struct_pointer_offset(), regs, handle,
                                               struct_array_vect[0], cur_struct_base);
    uint64_t length_from_struct = EvalOperand(instr.struct_length_offset(), regs, handle,
                                              struct_array_vect[0], cur_struct_base);
    std::vector<debug_ipc::MemoryBlock> inner_block_vect =
        handle.ReadMemoryBlocks(address_from_struct, length_from_struct);
    out_block_vect.insert(out_block_vect.begin(), inner_block_vect.begin(), inner_block_vect.end());
  }

  out_block_vect.insert(out_block_vect.begin(), struct_array_vect.begin(), struct_array_vect.end());
  return out_block_vect;
}

bool AutomationInstructionExecutor::EvalConditionVect(
    const std::vector<debug_ipc::AutomationCondition>& conditions, const GeneralRegisters& regs,
    const ProcessHandle& handle) {
  for (auto condition : conditions) {
    if (!EvalCondition(condition, regs, handle)) {
      return false;
    }
  }
  return true;
}

bool AutomationInstructionExecutor::EvalCondition(const debug_ipc::AutomationCondition& condition,
                                                  const GeneralRegisters& regs,
                                                  const ProcessHandle& handle) {
  switch (condition.kind()) {
    case debug_ipc::AutomationConditionKind::kFalse:
      return false;
    case debug_ipc::AutomationConditionKind::kEquals:
      return EvalOperand(condition.operand(), regs, handle) == condition.constant();
    case debug_ipc::AutomationConditionKind::kNotEquals:
      return EvalOperand(condition.operand(), regs, handle) != condition.constant();
    case debug_ipc::AutomationConditionKind::kMaskAndEquals:
      return (EvalOperand(condition.operand(), regs, handle) & condition.mask()) ==
             condition.constant();
    case debug_ipc::AutomationConditionKind::kMaskAndNotEquals:
      return (EvalOperand(condition.operand(), regs, handle) & condition.mask()) !=
             condition.constant();
  }
}

uint64_t AutomationInstructionExecutor::EvalOperand(const debug_ipc::AutomationOperand& operand,
                                                    const GeneralRegisters& regs,
                                                    const ProcessHandle& handle) {
  switch (operand.kind()) {
    case debug_ipc::AutomationOperandKind::kZero:
      return 0;
    case debug_ipc::AutomationOperandKind::kRegister:
      return regs.GetRegister(operand.register_index()).value_or(0);
    case debug_ipc::AutomationOperandKind::kConstant:
      return operand.value();
    case debug_ipc::AutomationOperandKind::kStackSlot: {
      uint64_t result = 0;
      size_t result_size;
      handle.ReadMemory(regs.sp() + operand.slot_offset(), &result, sizeof(result), &result_size);
      if (result_size == sizeof(result))
        return result;
      return 0;
    }
    case debug_ipc::AutomationOperandKind::kRegisterTimesConstant:
      return regs.GetRegister(operand.register_index()).value_or(0) * operand.value();
    case debug_ipc::AutomationOperandKind::kIndirectUInt32: {
      uint32_t result = 0;
      size_t result_size;
      handle.ReadMemory(regs.GetRegister(operand.register_index()).value_or(0) + operand.offset(),
                        &result, sizeof(result), &result_size);
      if (result_size == sizeof(result))
        return result;
      return 0;
    }
    case debug_ipc::AutomationOperandKind::kIndirectUInt64: {
      uint64_t result = 0;
      size_t result_size;
      handle.ReadMemory(regs.GetRegister(operand.register_index()).value_or(0) + operand.offset(),
                        &result, sizeof(result), &result_size);
      if (result_size == sizeof(result))
        return result;
      return 0;
    }
    case debug_ipc::AutomationOperandKind::kIndirectUInt32Loop:
      return 0;
    case debug_ipc::AutomationOperandKind::kIndirectUInt64Loop:
      return 0;
    case debug_ipc::AutomationOperandKind::kStoredValue:
      if (stored_values_.find(operand.slot_offset()) != stored_values_.end())
        return stored_values_.at(operand.slot_offset());
      return 0;
  }
}

uint64_t AutomationInstructionExecutor::EvalOperand(const debug_ipc::AutomationOperand& operand,
                                                    const GeneralRegisters& regs,
                                                    const ProcessHandle& handle,
                                                    const debug_ipc::MemoryBlock& loop_block,
                                                    uint64_t struct_base_pointer) {
  switch (operand.kind()) {
    case debug_ipc::AutomationOperandKind::kIndirectUInt32Loop: {
      return GetValueFromBytes<uint32_t>(loop_block.data, struct_base_pointer + operand.offset());
    }
    case debug_ipc::AutomationOperandKind::kIndirectUInt64Loop: {
      return GetValueFromBytes<uint64_t>(loop_block.data, struct_base_pointer + operand.offset());
    }
    default:
      return EvalOperand(operand, regs, handle);
  }
}

}  // namespace debug_agent
