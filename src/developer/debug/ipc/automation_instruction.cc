// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/automation_instruction.h"

#include <sstream>

#include "src/developer/debug/ipc/register_desc.h"

namespace debug_ipc {

std::string AutomationOperand::ToString() {
  std::stringstream output_stream;
  switch (kind_) {
    case AutomationOperandKind::kZero:
      return "zero";
    case AutomationOperandKind::kRegister:
      output_stream << debug_ipc::RegisterIDToString(static_cast<debug::RegisterID>(index_));
      return output_stream.str();
    case AutomationOperandKind::kConstant:
      output_stream << value_;
      return output_stream.str();
    case AutomationOperandKind::kStackSlot:
      output_stream << "[xsp + 0x" << std::hex << index_ << std::dec << "]/64";
      return output_stream.str();
    case AutomationOperandKind::kRegisterTimesConstant:
      output_stream << debug_ipc::RegisterIDToString(static_cast<debug::RegisterID>(index_))
                    << " * " << value_;
      return output_stream.str();
    case AutomationOperandKind::kIndirectUInt32:
      output_stream << "[" << debug_ipc::RegisterIDToString(static_cast<debug::RegisterID>(index_))
                    << " + 0x" << std::hex << value_ << std::dec << "]/32";
      return output_stream.str();
    case AutomationOperandKind::kIndirectUInt64:
      output_stream << "[" << debug_ipc::RegisterIDToString(static_cast<debug::RegisterID>(index_))
                    << " + 0x" << std::hex << value_ << std::dec << "]/64";
      return output_stream.str();
    case AutomationOperandKind::kIndirectUInt32Loop:
      output_stream << "[loop_offset + 0x" << std::hex << value_ << std::dec << "]/32";
      return output_stream.str();
    case AutomationOperandKind::kIndirectUInt64Loop:
      output_stream << "[loop_offset + 0x" << std::hex << value_ << std::dec << "]/64";
      return output_stream.str();
    case AutomationOperandKind::kStoredValue:
      output_stream << "stored_value(" << index_ << ")";
      return output_stream.str();
  }
}

std::string AutomationCondition::ToString() {
  std::stringstream output_stream;
  switch (kind_) {
    case AutomationConditionKind::kFalse:
      return "false";
    case AutomationConditionKind::kEquals:
      output_stream << operand_.ToString() << " == " << constant_;
      return output_stream.str();
    case AutomationConditionKind::kNotEquals:
      output_stream << operand_.ToString() << " != " << constant_;
      return output_stream.str();
    case AutomationConditionKind::kMaskAndEquals:
      output_stream << "(" << operand_.ToString() << " & 0x" << std::hex << mask_ << ") == 0x"
                    << constant_ << std::dec;
      return output_stream.str();
    case AutomationConditionKind::kMaskAndNotEquals:
      output_stream << "(" << operand_.ToString() << " & 0x" << std::hex << mask_ << ") != 0x"
                    << constant_ << std::dec;
      return output_stream.str();
  }
}

std::string AutomationInstruction::ToString() {
  std::stringstream output_stream;
  switch (kind_) {
    case AutomationInstructionKind::kNop:
      output_stream << "nop";
      break;
    case AutomationInstructionKind::kLoadMemory:
      output_stream << "load_memory " << address_.ToString() << ", " << length_.ToString();
      break;
    case AutomationInstructionKind::kLoopLoadMemory:
      output_stream << "loop_load_memory " << address_.ToString() << ", " << length_.ToString()
                    << ", " << extra_1_.ToString() << ", " << extra_2_.ToString() << ", " << value_;
      break;
    case AutomationInstructionKind::kComputeAndStore:
      output_stream << "stored_value(" << value_ << ") = " << extra_1_.ToString();
      break;
    case AutomationInstructionKind::kClearStoredValues:
      output_stream << "clear_stored_values";
      break;
  }
  if (!conditions_.empty()) {
    output_stream << ". conditions: ";
    const char* separator = "";
    for (auto condition : conditions_) {
      output_stream << separator << condition.ToString();
      separator = " && ";
    }
  }
  output_stream << "\n";
  return output_stream.str();
}

}  // namespace debug_ipc
