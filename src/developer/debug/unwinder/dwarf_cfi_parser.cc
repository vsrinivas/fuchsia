// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/dwarf_cfi_parser.h"

#include <cinttypes>
#include <cstdint>

#include "src/developer/debug/unwinder/error.h"
#include "src/developer/debug/unwinder/registers.h"

#define LOG_DEBUG(...)
// #define LOG_DEBUG printf

namespace unwinder {

DwarfCfiParser::DwarfCfiParser(Registers::Arch arch) {
  // Initialize those callee-preserved registers as kSameValue.
  static RegisterID kX64Preserved[] = {
      RegisterID::kX64_rbx, RegisterID::kX64_rbp, RegisterID::kX64_r12,
      RegisterID::kX64_r13, RegisterID::kX64_r14, RegisterID::kX64_r15,
  };
  // x18 (shadow call stack pointer) is not considered preserved, because even those SCS-enabled
  // functions do not generate CFI for x18, so the value is likely clobbered.
  //
  // LR/SP are considered to be preserved, because a function has to ensure that when the function
  // returns, the values in LR/SP are the same as when the function begins.
  static RegisterID kArm64Preserved[] = {
      RegisterID::kArm64_x19, RegisterID::kArm64_x20, RegisterID::kArm64_x21,
      RegisterID::kArm64_x22, RegisterID::kArm64_x23, RegisterID::kArm64_x24,
      RegisterID::kArm64_x25, RegisterID::kArm64_x26, RegisterID::kArm64_x27,
      RegisterID::kArm64_x28, RegisterID::kArm64_x29, RegisterID::kArm64_x30,
      RegisterID::kArm64_x31,
  };

  RegisterID* preserved;
  size_t length;
  switch (arch) {
    case Registers::Arch::kX64:
      preserved = kX64Preserved;
      length = sizeof(kX64Preserved) / sizeof(RegisterID);
      break;
    case Registers::Arch::kArm64:
      preserved = kArm64Preserved;
      length = sizeof(kArm64Preserved) / sizeof(RegisterID);
      break;
  }

  for (size_t i = 0; i < length; i++) {
    register_locations_[preserved[i]].type = RegisterLocation::Type::kSameValue;
  }
}

// Instruction              High 2 Bits  Low 6 Bits  Operand 1         Operand 2
// DW_CFA_advance_loc       0x1          delta
// DW_CFA_offset            0x2          register    ULEB128 offset
// DW_CFA_restore           0x3          register
// DW_CFA_set_loc           0            0x01        address
// DW_CFA_advance_loc1      0            0x02        1-byte delta
// DW_CFA_advance_loc2      0            0x03        2-byte delta
// DW_CFA_advance_loc4      0            0x04        4-byte delta
// DW_CFA_offset_extended   0            0x05        ULEB128 register  ULEB128 offset
// DW_CFA_restore_extended  0            0x06        ULEB128 register
// DW_CFA_undefined         0            0x07        ULEB128 register
// DW_CFA_same_value        0            0x08        ULEB128 register
// DW_CFA_register          0            0x09        ULEB128 register  ULEB128 register
// DW_CFA_remember_state    0            0x0a
// DW_CFA_restore_state     0            0x0b
// DW_CFA_def_cfa           0            0x0c        ULEB128 register  ULEB128 offset
// DW_CFA_def_cfa_register  0            0x0d        ULEB128 register
// DW_CFA_def_cfa_offset    0            0x0e        ULEB128 offset
// DW_CFA_nop               0            0
// DW_CFA_expression        0            0x10        ULEB128 register  BLOCK
// DW_CFA_lo_user           0            0x1c
// DW_CFA_hi_user           0            0x3f
Error DwarfCfiParser::ParseInstructions(Memory* elf, uint64_t code_alignment_factor,
                                        int64_t data_alignment_factor, uint64_t instructions_begin,
                                        uint64_t instructions_end, uint64_t pc_limit) {
  uint64_t pc = 0;
  while (instructions_begin < instructions_end && pc < pc_limit) {
    uint8_t opcode;
    LOG_DEBUG("%#" PRIx64 "   ", instructions_begin);
    if (auto err = elf->Read(instructions_begin, opcode); err.has_err()) {
      return err;
    }
    switch (opcode >> 6) {
      case 0x1: {  // DW_CFA_advance_loc
        LOG_DEBUG("DW_CFA_advance_loc %" PRId64 "\n", (opcode & 0x3F) * code_alignment_factor);
        pc += (opcode & 0x3F) * code_alignment_factor;
        continue;
      }
      case 0x2: {  // DW_CFA_offset
        uint64_t offset;
        if (auto err = elf->ReadULEB128(instructions_begin, offset); err.has_err()) {
          return err;
        }
        RegisterID reg = static_cast<RegisterID>(opcode & 0x3F);
        int64_t real_offset = static_cast<int64_t>(offset) * data_alignment_factor;
        LOG_DEBUG("DW_CFA_offset %hhu %" PRId64 "\n", reg, real_offset);
        register_locations_[reg].type = RegisterLocation::Type::kOffset;
        register_locations_[reg].offset = real_offset;
        continue;
      }
      case 0x3: {  // DW_CFA_restore
        LOG_DEBUG("DW_CFA_restore %d\n", opcode & 0x3F);
        RegisterID reg = static_cast<RegisterID>(opcode & 0x3F);
        register_locations_[reg] = initial_register_locations_[reg];
        continue;
      }
    }
    switch (opcode) {
      case 0x0: {  // DW_CFA_nop
        LOG_DEBUG("DW_CFA_nop\n");
        continue;
      }
      case 0x2: {  // DW_CFA_advance_loc1
        uint8_t delta;
        if (auto err = elf->Read(instructions_begin, delta); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_advance_loc1 %" PRId64 "\n", delta * code_alignment_factor);
        pc += delta * code_alignment_factor;
        continue;
      }
      case 0x3: {  // DW_CFA_advance_loc2
        uint16_t delta;
        if (auto err = elf->Read(instructions_begin, delta); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_advance_loc2 %" PRId64 "\n", delta * code_alignment_factor);
        pc += delta * code_alignment_factor;
        continue;
      }
      case 0x4: {  // DW_CFA_advance_loc4
        uint32_t delta;
        if (auto err = elf->Read(instructions_begin, delta); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_advance_loc4 %" PRId64 "\n", delta * code_alignment_factor);
        pc += delta * code_alignment_factor;
        continue;
      }
      case 0x7: {  // DW_CFA_undefined
        uint64_t reg;
        if (auto err = elf->ReadULEB128(instructions_begin, reg); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_undefined %" PRIu64 "\n", reg);
        register_locations_[static_cast<RegisterID>(reg)].type = RegisterLocation::Type::kUndefined;
        continue;
      }
      case 0x8: {  // DW_CFA_same_value
        uint64_t reg;
        if (auto err = elf->ReadULEB128(instructions_begin, reg); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_same_value %" PRIu64 "\n", reg);
        register_locations_[static_cast<RegisterID>(reg)].type = RegisterLocation::Type::kSameValue;
        continue;
      }
      case 0x9: {  // DW_CFA_register
        uint64_t reg1;
        if (auto err = elf->ReadULEB128(instructions_begin, reg1); err.has_err()) {
          return err;
        }
        uint64_t reg2;
        if (auto err = elf->ReadULEB128(instructions_begin, reg2); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_register %" PRIu64 " %" PRIu64 "\n", reg1, reg2);
        register_locations_[static_cast<RegisterID>(reg1)].type = RegisterLocation::Type::kRegister;
        register_locations_[static_cast<RegisterID>(reg1)].reg_id = static_cast<RegisterID>(reg2);
        continue;
      }
      case 0xC: {  // DW_CFA_def_cfa
        uint64_t reg;
        if (auto err = elf->ReadULEB128(instructions_begin, reg); err.has_err()) {
          return err;
        }
        cfa_register_ = static_cast<RegisterID>(reg);
        if (auto err = elf->ReadULEB128(instructions_begin, cfa_register_offset_); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_def_cfa %hhu %" PRIu64 "\n", cfa_register_, cfa_register_offset_);
        continue;
      }
      case 0xD: {  // DW_CFA_def_cfa_register
        uint64_t reg;
        if (auto err = elf->ReadULEB128(instructions_begin, reg); err.has_err()) {
          return err;
        }
        cfa_register_ = static_cast<RegisterID>(reg);
        LOG_DEBUG("DW_CFA_def_cfa_register %hhu\n", cfa_register_);
        continue;
      }
      case 0xE: {  // DW_CFA_def_cfa_offset
        if (auto err = elf->ReadULEB128(instructions_begin, cfa_register_offset_); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_def_cfa_offset %" PRIu64 "\n", cfa_register_offset_);
        continue;
      }
      case 0x10: {  // DW_CFA_expression
        uint64_t reg;
        if (auto err = elf->ReadULEB128(instructions_begin, reg); err.has_err()) {
          return err;
        }
        uint64_t length;
        if (auto err = elf->ReadULEB128(instructions_begin, length); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_expression length=%" PRIu64 "\n", length);
        register_locations_[static_cast<RegisterID>(reg)].type =
            RegisterLocation::Type::kExpression;
        // TODO(dangyi): add DWARF expression support.
        instructions_begin += length;
        continue;
      }
    }
    return Error("unsupported CFA instruction: %#x", opcode);
  }
  return Success();
}

Error DwarfCfiParser::Step(Memory* stack, RegisterID return_address_register,
                           const Registers& current, Registers& next) {
  if (cfa_register_ == RegisterID::kInvalid || cfa_register_offset_ == static_cast<uint64_t>(-1)) {
    return Error("undefined CFA");
  }

  uint64_t cfa;
  if (auto err = current.Get(cfa_register_, cfa); err.has_err()) {
    return err;
  }
  cfa += cfa_register_offset_;

  for (auto& [reg, location] : register_locations_) {
    switch (location.type) {
      case RegisterLocation::Type::kUndefined:
        break;
      case RegisterLocation::Type::kSameValue:
        // Allow failure here.
        if (uint64_t val; current.Get(reg, val).ok()) {
          next.Set(reg, val);
        }
        break;
      case RegisterLocation::Type::kRegister:
        // Allow failure here.
        if (uint64_t val; current.Get(location.reg_id, val).ok()) {
          next.Set(reg, val);
        }
        break;
      case RegisterLocation::Type::kOffset:
        // Allow failure here.
        if (uint64_t val; stack->Read(cfa + location.offset, val).ok()) {
          next.Set(reg, val);
        }
        break;
      case RegisterLocation::Type::kExpression:
        // TODO(dangyi): add DWARF expression support.
        break;
    }
  }

  // By definition, the CFA is the stack pointer at the call site, so restoring SP means setting it
  // to CFA.
  next.SetSP(cfa);

  // Return address is the address after the call instruction, so setting IP to that simualates a
  // return. On x64, return_address_register is just RIP so it's a noop. On arm64,
  // return_address_register is LR, which must be copied to IP.
  //
  // An unavailable return address, usually because of "DW_CFA_undefined: RIP/LR", marks the end of
  // the unwinding. We don't consider it an error.
  if (uint64_t return_address; next.Get(return_address_register, return_address).ok()) {
    // It's important to unset the return_address_register because we want to restore all registers
    // to the previous frame. While the value of return_address_register is changed during the call,
    // it's not possible to recover it any more. The same holds true when return_address_register is
    // IP, e.g., on x64.
    next.Unset(return_address_register);
    next.SetPC(return_address);
  }

  LOG_DEBUG("%s => %s\n", current.Describe().c_str(), next.Describe().c_str());
  return Success();
}

}  // namespace unwinder
