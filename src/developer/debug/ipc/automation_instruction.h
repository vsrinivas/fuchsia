// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_AUTOMATION_INSTRUCTION_H_
#define SRC_DEVELOPER_DEBUG_IPC_AUTOMATION_INSTRUCTION_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/shared/serialization.h"

namespace debug_ipc {

enum AutomationOperandKind : uint32_t {
  // This type is just used as a default value. It outputs zero when evaluated.
  kZero = 0,

  // A kRegister takes a register index (in index_), and outputs the 64 bit value stored in that
  // register.
  kRegister = 1,

  // A kConstant takes a uint32_t constant value (in value_), and outputs that value extended to a
  // uint64_t.
  kConstant = 2,

  // A kStackSlot takes an offset into the stack (in index_), and returns the 64 bit value at that
  // stack location.
  kStackSlot = 3,

  // A kRegisterTimesConstant takes a register index (in index_) and a constant value (in value_),
  // and outputs the value of that register multiplied by the value.
  kRegisterTimesConstant = 4,

  // a kIndirectUInt32 takes a register index (in index_) and a constant offset (in value_), and
  // outputs the 32 bit value at *(register + offset)
  kIndirectUInt32 = 5,

  // a kIndirectUInt64 takes a register index (in index_) and a constant offset (in value_), and
  // outputs the 64 bit value at *(register + offset)
  kIndirectUInt64 = 6,

  // a kIndirectUInt32Loop is a special operand that is only valid for kLoopLoadMemory instructions.
  // It takes a constant offset (in value_), and outputs the 32 bit value at
  // *(address + cur_struct + offset)
  kIndirectUInt32Loop = 7,

  // a kIndirectUInt64Loop is a special operand that is only valid for kLoopLoadMemory instructions.
  // It takes a constant offset (in value_), and outputs the 64 bit value at
  // *(address + cur_struct + offset)
  kIndirectUInt64Loop = 8,

  // A kStoredValue takes a slot in the list of stored values (in index_), and outputs the 64 bit
  // value stored in that slot.
  kStoredValue = 9,
};

struct AutomationOperand {
 public:
  void InitRegister(debug::RegisterID index) {
    kind_ = AutomationOperandKind::kRegister;
    index_ = static_cast<uint32_t>(index);
  }
  void InitConstant(uint32_t value) {
    kind_ = AutomationOperandKind::kConstant;
    value_ = value;
  }
  void InitStackSlot(uint32_t slot_offset) {
    kind_ = AutomationOperandKind::kStackSlot;
    index_ = slot_offset;
  }
  void InitRegisterTimesConstant(debug::RegisterID index, uint32_t value) {
    kind_ = AutomationOperandKind::kRegisterTimesConstant;
    index_ = static_cast<uint32_t>(index);
    value_ = value;
  }
  void InitIndirectUInt32(debug::RegisterID index, uint32_t value) {
    kind_ = AutomationOperandKind::kIndirectUInt32;
    index_ = static_cast<uint32_t>(index);
    value_ = value;
  }
  void InitIndirectUInt64(debug::RegisterID index, uint32_t value) {
    kind_ = AutomationOperandKind::kIndirectUInt64;
    index_ = static_cast<uint32_t>(index);
    value_ = value;
  }
  void InitIndirectUInt32Loop(uint32_t value) {
    kind_ = AutomationOperandKind::kIndirectUInt32Loop;
    value_ = value;
  }
  void InitIndirectUInt64Loop(uint32_t value) {
    kind_ = AutomationOperandKind::kIndirectUInt64Loop;
    value_ = value;
  }
  void InitStoredValue(uint32_t slot_offset) {
    kind_ = AutomationOperandKind::kStoredValue;
    index_ = slot_offset;
  }

  void InitRaw(AutomationOperandKind kind, uint32_t index, uint32_t value) {
    kind_ = kind;
    index_ = index;
    value_ = value;
  }

  // If the operand is a kRegister, this changes it to a kRegisterTimesConstant with value as the
  // constant. Otherwise, the register becomes kZero.
  void MultiplyValue(uint32_t value) {
    if (kind_ == AutomationOperandKind::kRegister) {
      kind_ = AutomationOperandKind::kRegisterTimesConstant;
      value_ = value;
    } else {
      kind_ = AutomationOperandKind::kZero;
      index_ = 0;
      value_ = 0;
    }
  }

  // If the operand is a kRegister, this changes it to a kIndirectUInt32 with value as the offset.
  // Otherwise, the register becomes kZero.
  void IndirectValue32(uint32_t value) {
    if (kind_ == AutomationOperandKind::kRegister) {
      kind_ = AutomationOperandKind::kIndirectUInt32;
      value_ = value;
    } else {
      kind_ = AutomationOperandKind::kZero;
      index_ = 0;
      value_ = 0;
    }
  }

  debug::RegisterID register_index() const { return static_cast<debug::RegisterID>(index_); }
  uint32_t slot_offset() const { return index_; }
  uint32_t index() const { return index_; }

  uint32_t value() const { return value_; }
  uint32_t offset() const { return value_; }

  AutomationOperandKind kind() const { return kind_; }

  std::string ToString();

  void Serialize(Serializer& ser, uint32_t ver) { ser | kind_ | index_ | value_; }

 private:
  AutomationOperandKind kind_ = AutomationOperandKind::kZero;
  uint32_t index_ = 0;
  uint32_t value_ = 0;
};

enum AutomationConditionKind : uint32_t {
  // This type is just used as a default value. It always returns false.
  kFalse = 0,

  // A kEquals condition takes an operand and a uint64 constant.
  // It is true when operand == constant.
  kEquals = 1,

  // A kNotEquals condition takes an operand and a uint64 constant.
  // It is true when operand != constant.
  kNotEquals = 2,

  // A kMaskAndEquals condition takes an operand, a uint64 mask, and a uint64 constant.
  // It is true when (operand & mask) == constant.
  kMaskAndEquals = 3,

  // A kMaskAndNotEquals condition takes an operand, a uint64 mask, and a uint64 constant.
  // It is true when (operand & mask) != constant.
  kMaskAndNotEquals = 4,
};

struct AutomationCondition {
 public:
  void InitEquals(AutomationOperand operand, uint64_t constant) {
    kind_ = AutomationConditionKind::kEquals;
    operand_ = operand;
    constant_ = constant;
  }
  void InitNotEquals(AutomationOperand operand, uint64_t constant) {
    kind_ = AutomationConditionKind::kNotEquals;
    operand_ = operand;
    constant_ = constant;
  }
  void InitMaskAndEquals(AutomationOperand operand, uint64_t constant, uint64_t mask) {
    kind_ = AutomationConditionKind::kMaskAndEquals;
    operand_ = operand;
    constant_ = constant;
    mask_ = mask;
  }
  void InitMaskAndNotEquals(AutomationOperand operand, uint64_t constant, uint64_t mask) {
    kind_ = AutomationConditionKind::kMaskAndNotEquals;
    operand_ = operand;
    constant_ = constant;
    mask_ = mask;
  }

  void InitRaw(AutomationConditionKind kind, AutomationOperand operand, uint64_t constant,
               uint64_t mask) {
    kind_ = kind;
    operand_ = operand;
    constant_ = constant;
    mask_ = mask;
  }

  std::string ToString();

  AutomationOperand operand() const { return operand_; }
  uint64_t constant() const { return constant_; }
  uint64_t mask() const { return mask_; }
  AutomationConditionKind kind() const { return kind_; }

  void Serialize(Serializer& ser, uint32_t ver) { ser | kind_ | operand_ | constant_ | mask_; }

 private:
  AutomationConditionKind kind_ = AutomationConditionKind::kFalse;
  AutomationOperand operand_;
  uint64_t constant_ = 0;
  uint64_t mask_ = 0;
};

enum AutomationInstructionKind : uint32_t {
  // This type is just used as a default value. Has no effect if sent.
  kNop = 0,

  // A kLoadMemory instruction takes two Operands:
  //  address (in address_) is the address of some memory.
  //  length (in length_) is the number of bytes to load from that memory.
  // It preloads length bytes from the memory at address.
  // It also takes a vector of conditions and only executes if it has no false conditions.
  kLoadMemory = 1,

  // A kLoopLoadMemory instruction takes four Operands and a uint32_t:
  //  address (in address_) is the address of an array of structs.
  //  length (in length_) is the number of structs in the array.
  //  struct_pointer_offset (in extra_1_) is the offset in the struct to the pointer to load from.
  //  struct_length_offset (in extra_2_) is the offset to the length of the memory to load.
  //  item_size (in value_) is the uint32_t size of the structs in the array in bytes.
  // First it preloads the array of structs (loading length * item_size bytes from address_).
  // Next it iterates through each of the structs, preloading the number of bytes specified at
  // address[index] + struct_size_offset from the address at address[index] +
  // struct_pointer_offset
  // It also takes a vector of conditions and only executes if it has no false conditions.
  kLoopLoadMemory = 2,

  // A kComputeAndStore instruction takes one Operand and a uint32_t:
  //  value (in extra_1_) is the operand to be stored.
  //  slot_index (in value_) is the slot index to store the result at.
  // It stores the value of the operand in that slot to be used by later operands. Nothing is
  // preloaded from this command.
  // It also takes a vector of conditions and only executes if it has no false conditions.
  kComputeAndStore = 3,

  // A kClearStoredValues instruction takes no Operands.
  // It clears all values stored by kComputeAndStore commands.
  // It also takes a vector of conditions and only executes if it has no false conditions.
  kClearStoredValues = 4,
};

// An instruction for automatically handling a breakpoint
struct AutomationInstruction {
 public:
  void InitLoadMemory(AutomationOperand address, AutomationOperand length,
                      std::vector<AutomationCondition> conditions) {
    kind_ = AutomationInstructionKind::kLoadMemory;
    address_ = address;
    length_ = length;
    conditions_ = std::move(conditions);
  }

  void InitLoopLoadMemory(AutomationOperand address, AutomationOperand length,
                          AutomationOperand struct_pointer_offset,
                          AutomationOperand struct_length_offset, uint32_t item_size,
                          std::vector<AutomationCondition> conditions) {
    kind_ = AutomationInstructionKind::kLoopLoadMemory;
    address_ = address;
    length_ = length;
    extra_1_ = struct_pointer_offset;
    extra_2_ = struct_length_offset;
    value_ = item_size;
    conditions_ = std::move(conditions);
  }

  void InitComputeAndStore(AutomationOperand value, uint32_t slot_index,
                           std::vector<AutomationCondition> conditions) {
    kind_ = AutomationInstructionKind::kComputeAndStore;
    extra_1_ = value;
    value_ = slot_index;
    conditions_ = std::move(conditions);
  }

  void InitClearStoredValues(std::vector<AutomationCondition> conditions) {
    kind_ = AutomationInstructionKind::kClearStoredValues;
    conditions_ = std::move(conditions);
  }

  void InitRaw(AutomationInstructionKind kind, AutomationOperand address, AutomationOperand length,
               AutomationOperand extra_1, AutomationOperand extra_2, uint32_t value,
               std::vector<AutomationCondition> conditions) {
    kind_ = kind;
    address_ = address;
    length_ = length;
    extra_1_ = extra_1;
    extra_2_ = extra_2;
    value_ = value;
    conditions_ = std::move(conditions);
  }

  AutomationOperand address() const { return address_; }

  AutomationOperand length() const { return length_; }

  AutomationOperand struct_pointer_offset() const { return extra_1_; }
  AutomationOperand store_value() const { return extra_1_; }
  AutomationOperand extra_1() const { return extra_1_; }

  AutomationOperand struct_length_offset() const { return extra_2_; }
  AutomationOperand extra_2() const { return extra_2_; }

  uint32_t item_size() const { return value_; }
  uint32_t slot_index() const { return value_; }
  uint32_t value() const { return value_; }

  std::vector<AutomationCondition> conditions() const { return conditions_; }
  AutomationInstructionKind kind() const { return kind_; }

  std::string ToString();

  void Serialize(Serializer& ser, uint32_t ver) {
    ser | kind_ | address_ | length_ | extra_1_ | extra_2_ | value_ | conditions_;
  }

 private:
  AutomationInstructionKind kind_ = AutomationInstructionKind::kNop;
  AutomationOperand address_;
  AutomationOperand length_;
  AutomationOperand extra_1_;
  AutomationOperand extra_2_;
  uint32_t value_ = 0;
  std::vector<AutomationCondition> conditions_;
};

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_AUTOMATION_INSTRUCTION_H_
