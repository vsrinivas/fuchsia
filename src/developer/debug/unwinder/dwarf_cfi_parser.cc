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

namespace {

// Read a RegisterID in ULEB128 encoding.
//
// Unwind tables could encode rules for registers that we don't support, e.g. float point or vector
// registers. It's safe to just set them to some invalid RegisterID (but don't overflow them),
// as Registers::Set will deny any unknown registers.
Error ReadRegisterID(Memory* elf, uint64_t& addr, RegisterID& reg) {
  uint64_t reg_id;
  if (auto err = elf->ReadULEB128(addr, reg_id); err.has_err()) {
    return err;
  }
  if (reg_id > static_cast<uint64_t>(RegisterID::kInvalid)) {
    return Error("register_id out of range");
  }
  reg = static_cast<RegisterID>(reg_id);
  return Success();
}

}  // namespace

DwarfCfiParser::DwarfCfiParser(Registers::Arch arch, uint64_t code_alignment_factor,
                               int64_t data_alignment_factor)
    : code_alignment_factor_(code_alignment_factor), data_alignment_factor_(data_alignment_factor) {
  // Initialize those callee-preserved registers as kSameValue.
  static RegisterID kX64Preserved[] = {
      RegisterID::kX64_rbx, RegisterID::kX64_rbp, RegisterID::kX64_r12,
      RegisterID::kX64_r13, RegisterID::kX64_r14, RegisterID::kX64_r15,
  };
  // x18 (shadow call stack pointer) is considered preserved. SCS-enabled functions will have
  // DW_CFA_val_expression rules for x18, and SCS-disabled functions don't touch x18.
  //
  // LR/SP are considered to be preserved, because a function has to ensure that when the function
  // returns, the values in LR/SP are the same as when the function begins.
  static RegisterID kArm64Preserved[] = {
      RegisterID::kArm64_x18, RegisterID::kArm64_x19, RegisterID::kArm64_x20,
      RegisterID::kArm64_x21, RegisterID::kArm64_x22, RegisterID::kArm64_x23,
      RegisterID::kArm64_x24, RegisterID::kArm64_x25, RegisterID::kArm64_x26,
      RegisterID::kArm64_x27, RegisterID::kArm64_x28, RegisterID::kArm64_x29,
      RegisterID::kArm64_x30, RegisterID::kArm64_x31,
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

// Instruction                High 2 Bits  Low 6 Bits  Operand 1         Operand 2
// DW_CFA_advance_loc         0x1          delta
// DW_CFA_offset              0x2          register    ULEB128 offset
// DW_CFA_restore             0x3          register
// DW_CFA_set_loc             0            0x01        address
// DW_CFA_advance_loc1        0            0x02        1-byte delta
// DW_CFA_advance_loc2        0            0x03        2-byte delta
// DW_CFA_advance_loc4        0            0x04        4-byte delta
// DW_CFA_offset_extended     0            0x05        ULEB128 register  ULEB128 offset
// DW_CFA_restore_extended    0            0x06        ULEB128 register
// DW_CFA_undefined           0            0x07        ULEB128 register
// DW_CFA_same_value          0            0x08        ULEB128 register
// DW_CFA_register            0            0x09        ULEB128 register  ULEB128 register
// DW_CFA_remember_state      0            0x0a
// DW_CFA_restore_state       0            0x0b
// DW_CFA_def_cfa             0            0x0c        ULEB128 register  ULEB128 offset
// DW_CFA_def_cfa_register    0            0x0d        ULEB128 register
// DW_CFA_def_cfa_offset      0            0x0e        ULEB128 offset
// DW_CFA_nop                 0            0
// DW_CFA_def_cfa_expression  0            0x0f        BLOCK
// DW_CFA_expression          0            0x10        ULEB128 register  BLOCK
// DW_CFA_offset_extended_sf  0            0x11        ULEB128 register  SLEB128 offset
// DW_CFA_def_cfa_sf          0            0x12        ULEB128 register  SLEB128 offset
// DW_CFA_def_cfa_offset_sf   0            0x13        SLEB128 offset
// DW_CFA_val_offset          0            0x14        ULEB128 register  ULEB128 offset
// DW_CFA_val_offset_sf       0            0x15        ULEB128 register  SLEB128 offset
// DW_CFA_val_expression      0            0x16        ULEB128 register  BLOCK
// DW_CFA_lo_user             0            0x1c
// DW_CFA_hi_user             0            0x3f
Error DwarfCfiParser::ParseInstructions(Memory* elf, uint64_t instructions_begin,
                                        uint64_t instructions_end, uint64_t pc_limit) {
  // Boundary is tricky here! Consider the following program
  //
  //         .cfi_startproc
  //     0:  push    rbp
  //         .cfi_def_cfa_offset 16
  //         .cfi_offset rbp, -16
  //     1:  mov     rbp, rsp
  //         .cfi_def_cfa_register rbp
  //     4:  call    f()
  //     9:  pop     rbp
  //         .cfi_def_cfa rsp, 8
  //    10:  ret
  //         .cfi_endproc
  //
  // ..which produces the following CFI.
  //
  //         DW_CFA_advance_loc: 1           // pc = 1
  //         DW_CFA_def_cfa_offset: +16
  //         DW_CFA_offset: RBP -16
  //         DW_CFA_advance_loc: 3           // pc = 4
  //         DW_CFA_def_cfa_register: RBP
  //         DW_CFA_advance_loc: 6           // pc = 10
  //         DW_CFA_def_cfa: RSP +8
  //
  // Suppose we have some exception at address 0x1 (pc_limit = 1), we want to stop at
  // "DW_CFA_advance_loc: 3" (pc = 4), not at "DW_CFA_advance_loc: 1" (pc = 1).
  uint64_t pc = 0;
  while (instructions_begin < instructions_end && pc <= pc_limit) {
    uint8_t opcode;
    LOG_DEBUG("%#" PRIx64 "   ", instructions_begin);
    if (auto err = elf->Read(instructions_begin, opcode); err.has_err()) {
      return err;
    }
    switch (opcode >> 6) {
      case 0x1: {  // DW_CFA_advance_loc
        LOG_DEBUG("DW_CFA_advance_loc %" PRId64 "\n", (opcode & 0x3F) * code_alignment_factor_);
        pc += (opcode & 0x3F) * code_alignment_factor_;
        continue;
      }
      case 0x2: {  // DW_CFA_offset
        uint64_t offset;
        if (auto err = elf->ReadULEB128(instructions_begin, offset); err.has_err()) {
          return err;
        }
        RegisterID reg = static_cast<RegisterID>(opcode & 0x3F);
        int64_t real_offset = static_cast<int64_t>(offset) * data_alignment_factor_;
        LOG_DEBUG("DW_CFA_offset %hhu %" PRId64 "\n", reg, real_offset);
        register_locations_[reg].type = RegisterLocation::Type::kOffset;
        register_locations_[reg].offset = real_offset;
        continue;
      }
      case 0x3: {  // DW_CFA_restore
        RegisterID reg = static_cast<RegisterID>(opcode & 0x3F);
        LOG_DEBUG("DW_CFA_restore %hhu\n", reg);
        register_locations_[reg] = initial_register_locations_[reg];
        continue;
      }
    }
    switch (opcode) {
      case 0x0: {  // DW_CFA_nop
        LOG_DEBUG("DW_CFA_nop\n");
        continue;
      }
      // case 0x1:  // DW_CFA_set_loc  address
      case 0x2: {  // DW_CFA_advance_loc1  1-byte delta
        uint8_t delta;
        if (auto err = elf->Read(instructions_begin, delta); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_advance_loc1 %" PRId64 "\n", delta * code_alignment_factor_);
        pc += delta * code_alignment_factor_;
        continue;
      }
      case 0x3: {  // DW_CFA_advance_loc2  2-byte delta
        uint16_t delta;
        if (auto err = elf->Read(instructions_begin, delta); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_advance_loc2 %" PRId64 "\n", delta * code_alignment_factor_);
        pc += delta * code_alignment_factor_;
        continue;
      }
      case 0x4: {  // DW_CFA_advance_loc4  4-byte delta
        uint32_t delta;
        if (auto err = elf->Read(instructions_begin, delta); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_advance_loc4 %" PRId64 "\n", delta * code_alignment_factor_);
        pc += delta * code_alignment_factor_;
        continue;
      }
      case 0x5: {  // DW_CFA_offset_extended  ULEB128 register  ULEB128 offset
        RegisterID reg;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg); err.has_err()) {
          return err;
        }
        uint64_t offset;
        if (auto err = elf->ReadULEB128(instructions_begin, offset); err.has_err()) {
          return err;
        }
        int64_t real_offset = static_cast<int64_t>(offset) * data_alignment_factor_;
        LOG_DEBUG("DW_CFA_offset_extended %hhu %" PRId64 "\n", reg, real_offset);
        register_locations_[reg].type = RegisterLocation::Type::kOffset;
        register_locations_[reg].offset = real_offset;
        continue;
      }
      case 0x6: {  // DW_CFA_restore_extended  ULEB128 register
        RegisterID reg;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_restore_extended %hhu\n", reg);
        register_locations_[reg] = initial_register_locations_[reg];
        continue;
      }
      case 0x7: {  // DW_CFA_undefined  ULEB128 register
        RegisterID reg;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_undefined %hhu\n", reg);
        register_locations_[reg].type = RegisterLocation::Type::kUndefined;
        continue;
      }
      case 0x8: {  // DW_CFA_same_value  ULEB128 register
        RegisterID reg;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_same_value %hhu\n", reg);
        register_locations_[reg].type = RegisterLocation::Type::kSameValue;
        continue;
      }
      case 0x9: {  // DW_CFA_register  ULEB128 register  ULEB128 register
        RegisterID reg1;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg1); err.has_err()) {
          return err;
        }
        RegisterID reg2;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg2); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_register %hhu %hhu\n", reg1, reg2);
        register_locations_[reg1].type = RegisterLocation::Type::kRegister;
        register_locations_[reg1].reg_id = reg2;
        continue;
      }
      case 0xA: {  // DW_CFA_remember_state
        LOG_DEBUG("DW_CFA_remember_state\n");
        state_stack_.emplace_back(cfa_location_, register_locations_);
        continue;
      }
      case 0xB: {  // DW_CFA_restore_state
        LOG_DEBUG("DW_CFA_restore_state\n");
        if (state_stack_.empty()) {
          return Error("invalid DW_CFA_restore_state");
        }
        std::tie(cfa_location_, register_locations_) = std::move(state_stack_.back());
        state_stack_.pop_back();
        continue;
      }
      case 0xC: {  // DW_CFA_def_cfa  ULEB128 register  ULEB128 offset
        if (auto err = ReadRegisterID(elf, instructions_begin, cfa_location_.reg); err.has_err()) {
          return err;
        }
        if (auto err = elf->ReadULEB128(instructions_begin, cfa_location_.offset); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_def_cfa %hhu %" PRIu64 "\n", cfa_location_.reg, cfa_location_.offset);
        continue;
      }
      case 0xD: {  // DW_CFA_def_cfa_register  ULEB128 register
        if (auto err = ReadRegisterID(elf, instructions_begin, cfa_location_.reg); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_def_cfa_register %hhu\n", cfa_location_.reg);
        continue;
      }
      case 0xE: {  // DW_CFA_def_cfa_offset  ULEB128 offset
        if (auto err = elf->ReadULEB128(instructions_begin, cfa_location_.offset); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_def_cfa_offset %" PRIu64 "\n", cfa_location_.offset);
        continue;
      }
      // case 0xF:  // DW_CFA_def_cfa_expression  BLOCK
      case 0x10: {  // DW_CFA_expression  ULEB128 register  BLOCK
        RegisterID reg;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg); err.has_err()) {
          return err;
        }
        uint64_t length;
        if (auto err = elf->ReadULEB128(instructions_begin, length); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_expression %hhu length=%" PRIu64 "\n", reg, length);
        register_locations_[reg].type = RegisterLocation::Type::kExpression;
        register_locations_[reg].expression = DwarfExpr(elf, instructions_begin, length);
        instructions_begin += length;
        continue;
      }
      // case 0x11:  // DW_CFA_offset_extended_sf  ULEB128 register  SLEB128 offset
      // case 0x12:  // DW_CFA_def_cfa_sf          ULEB128 register  SLEB128 offset
      // case 0x13:  // DW_CFA_def_cfa_offset_sf   SLEB128 offset
      // case 0x14:  // DW_CFA_val_offset          ULEB128 register  ULEB128 offset
      // case 0x15:  // DW_CFA_val_offset_sf       ULEB128 register  SLEB128 offset
      case 0x16: {  // DW_CFA_val_expression  ULEB128 register  BLOCK
        RegisterID reg;
        if (auto err = ReadRegisterID(elf, instructions_begin, reg); err.has_err()) {
          return err;
        }
        uint64_t length;
        if (auto err = elf->ReadULEB128(instructions_begin, length); err.has_err()) {
          return err;
        }
        LOG_DEBUG("DW_CFA_val_expression %hhu length=%" PRIu64 "\n", reg, length);
        register_locations_[reg].type = RegisterLocation::Type::kValExpression;
        register_locations_[reg].expression = DwarfExpr(elf, instructions_begin, length);
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
  if (cfa_location_.reg == RegisterID::kInvalid ||
      cfa_location_.offset == static_cast<uint64_t>(-1)) {
    return Error("undefined CFA");
  }

  uint64_t cfa;
  if (auto err = current.Get(cfa_location_.reg, cfa); err.has_err()) {
    return err;
  }
  cfa += cfa_location_.offset;

  for (auto& [reg, location] : register_locations_) {
    // Always allow failures when recovering individual registers.
    switch (location.type) {
      case RegisterLocation::Type::kUndefined:
        break;
      case RegisterLocation::Type::kSameValue:
        if (uint64_t val; current.Get(reg, val).ok()) {
          next.Set(reg, val);
        }
        break;
      case RegisterLocation::Type::kRegister:
        if (uint64_t val; current.Get(location.reg_id, val).ok()) {
          next.Set(reg, val);
        }
        break;
      case RegisterLocation::Type::kOffset:
        if (uint64_t val; stack && stack->Read(cfa + location.offset, val).ok()) {
          next.Set(reg, val);
        }
        break;
      case RegisterLocation::Type::kExpression:
        if (uint64_t loc; location.expression.Eval(stack, current, cfa, loc).ok()) {
          if (uint64_t val; stack && stack->Read(loc, val).ok()) {
            next.Set(reg, val);
          }
        }
        break;
      case RegisterLocation::Type::kValExpression:
        if (uint64_t val; location.expression.Eval(stack, current, cfa, val).ok()) {
          next.Set(reg, val);
        }
        break;
    }
  }

  // By definition, the CFA is the stack pointer at the call site, so restoring SP means setting it
  // to CFA.
  next.SetSP(cfa);

  // Return address is the address after the call instruction, so setting IP to that simulates a
  // return. On x64, return_address_register is just RIP so it's a noop. On arm64,
  // return_address_register is LR, which must be copied to IP.
  //
  // An unavailable return address, usually because of "DW_CFA_undefined: RIP/LR", marks the end of
  // the unwinding. We don't consider it an error.
  if (uint64_t return_address; next.Get(return_address_register, return_address).ok()) {
    // It's important to unset the return_address_register because we want to restore all registers
    // to the previous frame. Since the value of return_address_register is changed during the call,
    // it's not possible to recover it any more. The same holds true when return_address_register is
    // IP, e.g., on x64.
    next.Unset(return_address_register);
    next.SetPC(return_address);
  }

  LOG_DEBUG("%s => %s\n", current.Describe().c_str(), next.Describe().c_str());
  return Success();
}

}  // namespace unwinder
