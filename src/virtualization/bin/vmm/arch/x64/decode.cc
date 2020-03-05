// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/decode.h"

#include <string.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

static constexpr uint8_t kRexRMask = 1u << 2;
static constexpr uint8_t kRexWMask = 1u << 3;
static constexpr uint8_t kModRMRegMask = 0b00111000;
// The Operand Size (w) Bit.
static constexpr uint8_t kWMask = 1u;
static constexpr uint8_t kSibBaseMask = 0b00000111;
static constexpr uint8_t kSibBaseNone = 0b101;

static bool is_h66_prefix(uint8_t prefix) { return prefix == 0x66; }

static bool is_rex_prefix(uint8_t prefix) { return (prefix >> 4) == 0b0100; }

static bool has_sib_byte(uint8_t mod_rm) {
  return (mod_rm >> 6) != 0b11 && (mod_rm & 0b111) == 0b100;
}

static uint8_t displacement_size(uint8_t mod_rm, uint8_t sib) {
  switch (mod_rm >> 6) {
    case 0b00:
      if (has_sib_byte(mod_rm) && (sib & kSibBaseMask) == kSibBaseNone) {
        return 4;
      } else {
        return 0;
      }
    case 0b01:
      return 1;
    case 0b10:
      return 4;
    default:
      return (mod_rm & ~kModRMRegMask) == 0b00000101 ? 4 : 0;
  }
}

static uint8_t operand_size(bool h66, bool rex_w, bool w, uint8_t default_operand_size) {
  if (!w) {
    return 1;
  } else if (rex_w) {
    return 8;
  }
  if (!h66) {
    return default_operand_size;
  } else {
    return default_operand_size == 2 ? 4 : 2;
  }
}

static uint8_t immediate_size(bool h66, bool w, uint8_t default_operand_size) {
  if (!w) {
    return 1;
  } else if (!h66) {
    return default_operand_size;
  } else {
    return default_operand_size == 2 ? 4 : 2;
  }
}

static uint8_t register_id(uint8_t mod_rm, bool rex_r) {
  return static_cast<uint8_t>(((mod_rm >> 3) & 0b111) + (rex_r ? 0b1000 : 0));
}

// From Intel Volume 2, Appendix B.1.4.1
//
// Registers 4-7 (typically referring to SP,BP,SI,DI) instead refer to the
// high byte registers (AH,CH,DH,BH) when using 1 byte registers and no rex
// prefix is provided.
static inline bool is_high_byte(uint8_t size, bool rex) { return size == 1 && !rex; }

static uint64_t* select_register(zx_vcpu_state_t* vcpu_state, uint8_t register_id, uint8_t size,
                                 bool rex) {
  // From Intel Volume 2, Section 2.1.
  switch (register_id) {
    // From Intel Volume 2, Section 2.1.5.
    case 0:
      return &vcpu_state->rax;
    case 1:
      return &vcpu_state->rcx;
    case 2:
      return &vcpu_state->rdx;
    case 3:
      return &vcpu_state->rbx;
    case 4:
      if (is_high_byte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state->rsp;
    case 5:
      if (is_high_byte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state->rbp;
    case 6:
      if (is_high_byte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state->rsi;
    case 7:
      if (is_high_byte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state->rdi;
    case 8:
      return &vcpu_state->r8;
    case 9:
      return &vcpu_state->r9;
    case 10:
      return &vcpu_state->r10;
    case 11:
      return &vcpu_state->r11;
    case 12:
      return &vcpu_state->r12;
    case 13:
      return &vcpu_state->r13;
    case 14:
      return &vcpu_state->r14;
    case 15:
      return &vcpu_state->r15;
    default:
      return NULL;
  }
}

static zx_status_t deconstruct_instruction(const uint8_t* inst_buf, uint32_t inst_len,
                                           uint16_t* opcode, uint8_t* mod_rm, uint8_t* sib) {
  if (inst_len == 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  switch (inst_buf[0]) {
    case 0x0f:
      if (inst_len < 3) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      // Use memcpy instead of casting, otherwise we may cause an unaligned
      // access, resulting in undefined behaviour.
      memcpy(opcode, inst_buf, sizeof(uint16_t));
      *mod_rm = inst_buf[2];
      if (!has_sib_byte(*mod_rm)) {
        *sib = 0;
      } else if (inst_len < 4) {
        return ZX_ERR_NOT_SUPPORTED;
      } else {
        *sib = inst_buf[3];
      }
      break;
    default:
      if (inst_len < 2) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      *opcode = inst_buf[0];
      *mod_rm = inst_buf[1];
      if (!has_sib_byte(*mod_rm)) {
        *sib = 0;
      } else if (inst_len < 3) {
        return ZX_ERR_NOT_SUPPORTED;
      } else {
        *sib = inst_buf[2];
      }
      break;
  }
  return ZX_OK;
}

// Decode an instruction used in a memory access to determine the register used
// as a source or destination. There's no need to decode memory operands because
// the faulting address is already known.
zx_status_t inst_decode(const uint8_t* inst_buf, uint32_t inst_len, uint8_t default_operand_size,
                        zx_vcpu_state_t* vcpu_state, Instruction* inst) {
  if (inst_len == 0) {
    return ZX_ERR_BAD_STATE;
  }
  if (inst_len > X86_MAX_INST_LEN) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (default_operand_size != 2 && default_operand_size != 4) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (vcpu_state == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Parse 66H prefix.
  bool h66 = is_h66_prefix(inst_buf[0]);
  if (h66) {
    if (inst_len == 1) {
      return ZX_ERR_BAD_STATE;
    }
    inst_buf++;
    inst_len--;
  }
  // Parse REX prefix.
  //
  // From Intel Volume 2, Appendix 2.2.1: Only one REX prefix is allowed per
  // instruction. If used, the REX prefix byte must immediately precede the
  // opcode byte or the escape opcode byte (0FH).
  bool rex = false;
  bool rex_r = false;
  bool rex_w = false;
  if (is_rex_prefix(inst_buf[0])) {
    rex = true;
    rex_r = inst_buf[0] & kRexRMask;
    rex_w = inst_buf[0] & kRexWMask;
    inst_buf++;
    inst_len--;
  }
  // Technically this is valid, but no sane compiler should emit it.
  if (h66 && rex_w) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint16_t opcode;
  uint8_t mod_rm;
  uint8_t sib;
  zx_status_t status = deconstruct_instruction(inst_buf, inst_len, &opcode, &mod_rm, &sib);
  if (status != ZX_OK) {
    return status;
  }

  const uint8_t sib_size = has_sib_byte(mod_rm) ? 1 : 0;
  const uint8_t disp_size = displacement_size(mod_rm, sib);
  switch (opcode) {
    // Move r to r/m.
    // 1000 100w : mod reg r/m
    case 0x88:
    case 0x89: {
      if (inst_len != sib_size + disp_size + 2u) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      const bool w = opcode & kWMask;
      inst->type = INST_MOV_WRITE;
      inst->access_size = operand_size(h66, rex_w, w, default_operand_size);
      inst->imm = 0;
      inst->reg = select_register(vcpu_state, register_id(mod_rm, rex_r), inst->access_size, rex);
      inst->flags = NULL;
      return inst->reg == NULL ? ZX_ERR_NOT_SUPPORTED : ZX_OK;
    }
    // Move r/m to r.
    // 1000 101w : mod reg r/m
    case 0x8a:
    case 0x8b: {
      if (inst_len != sib_size + disp_size + 2u) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      const bool w = opcode & kWMask;
      inst->type = INST_MOV_READ;
      inst->access_size = operand_size(h66, rex_w, w, default_operand_size);
      inst->imm = 0;
      inst->reg = select_register(vcpu_state, register_id(mod_rm, rex_r), inst->access_size, rex);
      inst->flags = NULL;
      return inst->reg == NULL ? ZX_ERR_NOT_SUPPORTED : ZX_OK;
    }
    // Move imm to r/m.
    // 1100 011w : mod 000 r/m : immediate data
    case 0xc6:
    case 0xc7: {
      const bool w = opcode & kWMask;
      const uint8_t imm_size = immediate_size(h66, w, default_operand_size);
      if (inst_len != sib_size + disp_size + imm_size + 2u) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      if ((mod_rm & kModRMRegMask) != 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      inst->type = INST_MOV_WRITE;
      inst->access_size = operand_size(h66, rex_w, w, default_operand_size);
      inst->imm = 0;
      inst->reg = NULL;
      inst->flags = NULL;
      memcpy(&inst->imm, inst_buf + sib_size + disp_size + 2, imm_size);
      return ZX_OK;
    }
    // Move (16-bit) with zero-extend r/m to r.
    case 0xb70f:
      if (h66) {
        return ZX_ERR_BAD_STATE;
      }
    // Move (8-bit) with zero-extend r/m to r.
    case 0xb60f: {
      if (inst_len != sib_size + disp_size + 3u) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      const bool w = opcode & (kWMask << 8);

      // We'll be operating with different sized operands due to the zero-
      // extend. The 'w' bit determines if we're reading 8 or 16 bits out of
      // memory while the h66/rex_w bits and default operand size are used to
      // select the destination register size.
      const uint8_t access_size = w ? 2 : 1;
      const uint8_t reg_size = operand_size(h66, rex_w, true, default_operand_size);
      inst->type = INST_MOV_READ;
      inst->access_size = access_size;
      inst->imm = 0;
      inst->reg = select_register(vcpu_state, register_id(mod_rm, rex_r), reg_size, rex);
      inst->flags = NULL;
      return inst->reg == NULL ? ZX_ERR_NOT_SUPPORTED : ZX_OK;
    }
    // Logical compare (8-bit) imm with r/m.
    case 0xf6:
      if (h66) {
        return ZX_ERR_BAD_STATE;
      }
      if (inst_len != sib_size + disp_size + 3u) {
        return ZX_ERR_OUT_OF_RANGE;
      }
      if ((mod_rm & kModRMRegMask) != 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      inst->type = INST_TEST;
      inst->access_size = 1;
      inst->imm = 0;
      inst->reg = NULL;
      inst->flags = &vcpu_state->rflags;
      memcpy(&inst->imm, inst_buf + sib_size + disp_size + 2, 1);
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}
