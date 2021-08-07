// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/automation_instruction.h"

namespace debug_ipc {

TEST(AutomationInstruction, OperandToString) {
  AutomationOperand operand;
  EXPECT_EQ(operand.ToString(), "zero");

  operand.InitRegister(debug::RegisterID::kX64_rax);
  EXPECT_EQ(operand.ToString(), "rax");

  operand.InitConstant(12345);
  EXPECT_EQ(operand.ToString(), "12345");

  operand.InitStackSlot(0x10);
  EXPECT_EQ(operand.ToString(), "[xsp + 0x10]/64");

  operand.InitRegisterTimesConstant(debug::RegisterID::kARMv8_x0, 32);
  EXPECT_EQ(operand.ToString(), "x0 * 32");

  operand.InitIndirectUInt32(debug::RegisterID::kARMv8_x1, 0x40);
  EXPECT_EQ(operand.ToString(), "[x1 + 0x40]/32");

  operand.InitIndirectUInt64(debug::RegisterID::kARMv8_x2, 0x80);
  EXPECT_EQ(operand.ToString(), "[x2 + 0x80]/64");

  operand.InitIndirectUInt32Loop(0x100);
  EXPECT_EQ(operand.ToString(), "[loop_offset + 0x100]/32");

  operand.InitIndirectUInt64Loop(0x200);
  EXPECT_EQ(operand.ToString(), "[loop_offset + 0x200]/64");

  operand.InitStoredValue(1024);
  EXPECT_EQ(operand.ToString(), "stored_value(1024)");
}

TEST(AutomationInstruction, ConditionToString) {
  AutomationOperand operand;
  operand.InitRegister(debug::RegisterID::kX64_rcx);
  AutomationCondition condition;
  EXPECT_EQ(condition.ToString(), "false");

  condition.InitEquals(operand, 16);
  EXPECT_EQ(condition.ToString(), "rcx == 16");

  condition.InitNotEquals(operand, 32);
  EXPECT_EQ(condition.ToString(), "rcx != 32");

  condition.InitMaskAndEquals(operand, 0x40, 0x400);
  EXPECT_EQ(condition.ToString(), "(rcx & 0x400) == 0x40");

  condition.InitMaskAndNotEquals(operand, 0x80, 0x800);
  EXPECT_EQ(condition.ToString(), "(rcx & 0x800) != 0x80");
}

TEST(AutomationInstruction, InstructionToString) {
  AutomationOperand operand_1;
  AutomationOperand operand_2;
  AutomationOperand operand_3;
  AutomationOperand operand_4;
  AutomationOperand operand_5;
  operand_1.InitConstant(12345);
  operand_2.InitConstant(67890);
  operand_3.InitIndirectUInt32Loop(128);
  operand_4.InitIndirectUInt64Loop(256);
  operand_5.InitRegister(debug::RegisterID::kX64_rdx);
  std::vector<AutomationCondition> condition_vect;
  AutomationInstruction instruction;
  EXPECT_EQ(instruction.ToString(), "nop\n");

  condition_vect.emplace_back(AutomationCondition());
  condition_vect[0].InitEquals(operand_5, 12345);
  instruction.InitLoadMemory(operand_1, operand_2, condition_vect);
  EXPECT_EQ(instruction.ToString(), "load_memory 12345, 67890. conditions: rdx == 12345\n");

  condition_vect.emplace_back(AutomationCondition());
  condition_vect[1].InitNotEquals(operand_5, 54321);
  instruction.InitLoopLoadMemory(operand_1, operand_2, operand_3, operand_4, 32, condition_vect);
  EXPECT_EQ(instruction.ToString(),
            "loop_load_memory 12345, 67890, [loop_offset + 0x80]/32, [loop_offset + 0x100]/64, 32. "
            "conditions: rdx == 12345 && rdx != 54321\n");

  condition_vect[0].InitMaskAndEquals(operand_5, 0x1000, 0x1000);
  instruction.InitComputeAndStore(operand_1, 64, condition_vect);
  EXPECT_EQ(instruction.ToString(),
            "stored_value(64) = 12345. conditions: (rdx & 0x1000) == 0x1000 && rdx != 54321\n");

  condition_vect[1].InitMaskAndNotEquals(operand_5, 0x400, 0x400);
  instruction.InitClearStoredValues(condition_vect);
  EXPECT_EQ(
      instruction.ToString(),
      "clear_stored_values. conditions: (rdx & 0x1000) == 0x1000 && (rdx & 0x400) != 0x400\n");
}

}  // namespace debug_ipc
