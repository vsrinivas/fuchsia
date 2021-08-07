// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/automation_instruction_executor.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/mock_debug_agent_harness.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_process_handle.h"

namespace debug_agent {
TEST(AutomationInstructionExecutorTest, OperandEvaluation) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid = 12;
  MockProcess* process = harness.AddProcess(kProcKoid);
  AutomationInstructionExecutor executor;

  constexpr uint64_t kDynamicMemoryAddress = 0xfeed1dad;
  constexpr uint64_t kStackMemoryAddress = 0xdeadbeef;

  constexpr uint32_t kValue = 8;
  constexpr uint64_t kRegisterValue = 2;

  MockProcessHandle handle = process->mock_process_handle();

  // This is memory for the automated breakpoint to read.
  std::vector<uint8_t> mock_dynamic_memory = {100, 101, 102, 103, 104, 105, 106, 107,
                                              108, 109, 110, 111, 112, 113, 114, 115};
  handle.mock_memory().AddMemory(kDynamicMemoryAddress, mock_dynamic_memory);

  std::vector<uint8_t> mock_stack_memory = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  handle.mock_memory().AddMemory(kStackMemoryAddress, mock_stack_memory);

  GeneralRegisters regs;

  regs.set_sp(kStackMemoryAddress);
#if defined(__x86_64__)
  regs.GetNativeRegisters().rdi = kDynamicMemoryAddress;
  regs.GetNativeRegisters().rsi = kRegisterValue;
  constexpr debug::RegisterID register_id_1 = debug::RegisterID::kX64_rdi;
  constexpr debug::RegisterID register_id_2 = debug::RegisterID::kX64_rsi;
#elif defined(__aarch64__)
  regs.GetNativeRegisters().r[0] = kDynamicMemoryAddress;
  regs.GetNativeRegisters().r[1] = kRegisterValue;
  constexpr debug::RegisterID register_id_1 = debug::RegisterID::kARMv8_x0;
  constexpr debug::RegisterID register_id_2 = debug::RegisterID::kARMv8_x1;
#endif

  debug_ipc::AutomationOperand operand;
  // This tests the default value of operand, kZero, which should always return 0.
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle), 0ul);

  operand.InitRegister(register_id_1);
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle), kDynamicMemoryAddress);

  operand.InitConstant(kValue);
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle), kValue);

  operand.InitStackSlot(kValue);
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle),
            AutomationInstructionExecutor::GetValueFromBytes<uint64_t>(mock_stack_memory, kValue));

  operand.InitRegisterTimesConstant(register_id_2, kValue);
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle), kRegisterValue * kValue);

  operand.InitIndirectUInt32(register_id_1, kValue);
  EXPECT_EQ(
      executor.EvalOperand(operand, regs, handle),
      AutomationInstructionExecutor::GetValueFromBytes<uint32_t>(mock_dynamic_memory, kValue));

  operand.InitIndirectUInt64(register_id_1, kValue);
  EXPECT_EQ(
      executor.EvalOperand(operand, regs, handle),
      AutomationInstructionExecutor::GetValueFromBytes<uint64_t>(mock_dynamic_memory, kValue));

  executor.stored_values().emplace(kValue, 12345);
  operand.InitStoredValue(kValue);
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle), 12345ul);

  std::vector<uint8_t> mock_block_memory = {200, 201, 202, 203, 204, 205, 206, 207,
                                            208, 209, 210, 211, 212, 213, 214, 215};
  debug_ipc::MemoryBlock mock_loop_block;
  mock_loop_block.data = mock_block_memory;
  mock_loop_block.address = kDynamicMemoryAddress;
  mock_loop_block.size = mock_block_memory.size();
  mock_loop_block.valid = true;

  constexpr uint64_t mock_struct_base = 2;

  operand.InitIndirectUInt32Loop(2);
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle, mock_loop_block, mock_struct_base),
            AutomationInstructionExecutor::GetValueFromBytes<uint32_t>(mock_block_memory,
                                                                       mock_struct_base + 2));

  operand.InitIndirectUInt64Loop(6);
  EXPECT_EQ(executor.EvalOperand(operand, regs, handle, mock_loop_block, mock_struct_base),
            AutomationInstructionExecutor::GetValueFromBytes<uint64_t>(mock_block_memory,
                                                                       mock_struct_base + 6));
}

TEST(AutomationInstructionExecutorTest, ConditionEvaluation) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid = 12;
  MockProcess* process = harness.AddProcess(kProcKoid);
  MockProcessHandle handle = process->mock_process_handle();
  GeneralRegisters regs;
  AutomationInstructionExecutor executor;

  constexpr uint32_t kValue = 14;
  constexpr uint32_t kMask = 5;

  debug_ipc::AutomationOperand operand;
  operand.InitConstant(kValue);

  debug_ipc::AutomationCondition condition;
  // This tests the default value of condition, kFalse, which should always return false.
  EXPECT_FALSE(executor.EvalCondition(condition, regs, handle));

  condition.InitEquals(operand, kValue);
  EXPECT_TRUE(executor.EvalCondition(condition, regs, handle));

  condition.InitNotEquals(operand, kValue + 1);
  EXPECT_TRUE(executor.EvalCondition(condition, regs, handle));

  condition.InitMaskAndEquals(operand, kValue & kMask, kMask);
  EXPECT_TRUE(executor.EvalCondition(condition, regs, handle));

  condition.InitMaskAndNotEquals(operand, (kValue & kMask) + 1, kMask);
  EXPECT_TRUE(executor.EvalCondition(condition, regs, handle));

  std::vector<debug_ipc::AutomationCondition> condition_vect;
  condition_vect.emplace_back();
  condition_vect.emplace_back();
  condition_vect.emplace_back();

  condition_vect[0].InitEquals(operand, kValue);
  condition_vect[1].InitNotEquals(operand, kValue - 1);

  EXPECT_FALSE(executor.EvalConditionVect(condition_vect, regs, handle));

  condition_vect[2].InitMaskAndEquals(operand, kValue & kMask, kMask);

  EXPECT_TRUE(executor.EvalConditionVect(condition_vect, regs, handle));
}

TEST(AutomationInstructionExecutorTest, InstructionEvaluation) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid = 12;
  MockProcess* process = harness.AddProcess(kProcKoid);
  MockProcessHandle handle = process->mock_process_handle();
  AutomationInstructionExecutor executor;

  constexpr uint64_t kDynamicMemoryAddress = 0xfeed1dad;
  constexpr uint64_t kStructMemoryAddress = 0xdeadbeef;
  constexpr uint32_t kLength = 2;
  constexpr uint32_t kSize = 12;
  constexpr uint64_t kOffset = 8;
  constexpr uint32_t kIndex = 1234;

  // This is memory for the automated breakpoint to read.
  std::vector<uint8_t> mock_dynamic_memory = {100, 101, 102, 103, 104, 105, 106, 107,
                                              108, 109, 110, 111, 112, 113, 114, 115};
  handle.mock_memory().AddMemory(kDynamicMemoryAddress, mock_dynamic_memory);

  // This contains a pointer to the kDynamicMemoryAddress, followed by uint32_t(2). The second
  // address is kDynamicMemoryAddress+2.
  std::vector<uint8_t> mock_struct_memory = {
      0xad,           0x1d, 0xed, 0xfe, 0x00, 0x00, 0x00, 0x00, kLength, 0x00, 0x00, 0x00,
      0xad + kLength, 0x1d, 0xed, 0xfe, 0x00, 0x00, 0x00, 0x00, kLength, 0x00, 0x00, 0x00,
  };
  handle.mock_memory().AddMemory(kStructMemoryAddress, mock_struct_memory);

  GeneralRegisters regs;

  debug_ipc::AutomationOperand address;
  debug_ipc::AutomationOperand struct_address;
  debug_ipc::AutomationOperand length;
  debug_ipc::AutomationOperand struct_pointer_offset;
  debug_ipc::AutomationOperand struct_length_offset;
  address.InitConstant(kDynamicMemoryAddress);
  struct_address.InitConstant(kStructMemoryAddress);
  length.InitConstant(kLength);
  struct_pointer_offset.InitIndirectUInt64Loop(0);
  struct_length_offset.InitIndirectUInt32Loop(kOffset);

  std::vector<debug_ipc::AutomationCondition> condition_vect;

  std::vector<debug_ipc::MemoryBlock> block_vect;

  std::vector<debug_ipc::AutomationInstruction> instruction_vect;
  instruction_vect.emplace_back();

  // This tests the default value of instruction, kNop, which should always return nothing.
  block_vect = executor.ExecuteInstructionVect(instruction_vect, regs, handle);
  ASSERT_EQ(block_vect.size(), 0ul);
  EXPECT_EQ(executor.stored_values().size(), 0ul);

  // Test LoadMemory
  instruction_vect[0].InitLoadMemory(address, length, condition_vect);
  block_vect = executor.ExecuteInstructionVect(instruction_vect, regs, handle);
  ASSERT_EQ(block_vect.size(), 1ul);
  for (uint32_t i = 0; i < kLength; i++) {
    EXPECT_EQ(block_vect[0].data[i], mock_dynamic_memory[i]);
  }
  EXPECT_EQ(executor.stored_values().size(), 0ul);

  // Test LoopLoadMemory
  instruction_vect[0].InitLoopLoadMemory(struct_address, length, struct_pointer_offset,
                                         struct_length_offset, kSize, condition_vect);
  block_vect = executor.ExecuteInstructionVect(instruction_vect, regs, handle);
  ASSERT_EQ(block_vect.size(), kLength + 1);
  for (const auto& block : block_vect) {
    if (block.address == kStructMemoryAddress) {
      ASSERT_EQ(block.size, kLength * kSize);
      for (uint32_t i = 0; i < kLength * kSize; i++) {
        EXPECT_EQ(block.data[i], mock_struct_memory[i]);
      }
    } else {
      ASSERT_EQ(block.size, kLength);
      uint32_t dynamic_offset = block.address - kDynamicMemoryAddress;
      for (uint32_t i = 0; i < kLength; i++) {
        EXPECT_EQ(block.data[i], mock_dynamic_memory[i + dynamic_offset]);
      }
    }
  }
  EXPECT_EQ(executor.stored_values().size(), 0ul);

  // Test ComputeAndStore and ClearStoredValues
  instruction_vect[0].InitComputeAndStore(address, kIndex, condition_vect);
  block_vect = executor.ExecuteInstructionVect(instruction_vect, regs, handle);
  EXPECT_EQ(block_vect.size(), 0ul);
  ASSERT_EQ(executor.stored_values().size(), 1ul);

  debug_ipc::AutomationOperand stored_value;
  stored_value.InitStoredValue(kIndex);
  EXPECT_EQ(executor.EvalOperand(stored_value, regs, handle), kDynamicMemoryAddress);

  instruction_vect[0].InitClearStoredValues(condition_vect);
  block_vect = executor.ExecuteInstructionVect(instruction_vect, regs, handle);
  EXPECT_EQ(block_vect.size(), 0ul);
  ASSERT_EQ(executor.stored_values().size(), 0ul);

  // setup for the conditional tests
  condition_vect.emplace_back();

  // Test condition_vect allowing execution
  condition_vect[0].InitEquals(length, kLength);  // always true
  instruction_vect[0].InitLoadMemory(address, length, condition_vect);
  block_vect = executor.ExecuteInstructionVect(instruction_vect, regs, handle);
  ASSERT_EQ(block_vect.size(), 1ul);
  for (uint32_t i = 0; i < kLength; i++) {
    EXPECT_EQ(block_vect[0].data[i], mock_dynamic_memory[i]);
  }
  EXPECT_EQ(executor.stored_values().size(), 0ul);

  // Test condition_vect preventing execution
  condition_vect[0].InitEquals(length, kLength + 1);  // always false
  instruction_vect[0].InitLoadMemory(address, length, condition_vect);
  block_vect = executor.ExecuteInstructionVect(instruction_vect, regs, handle);
  ASSERT_EQ(block_vect.size(), 0ul);
  EXPECT_EQ(executor.stored_values().size(), 0ul);
}
}  // namespace debug_agent
