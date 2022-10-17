// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/decode.h"

namespace {

constexpr uint8_t kRexRMask = 1u << 2;
constexpr uint8_t kRexWMask = 1u << 3;
constexpr uint8_t kModRMRegMask = 0b00111000;
// The Operand Size (w) Bit.
constexpr uint8_t kWMask = 1u;
constexpr uint8_t kSibBaseMask = 0b00000111;
constexpr uint8_t kSibBaseNone = 0b101;
constexpr uint8_t kModRegToRegAddressing = 0b11;

// Get the "mod" bits from a ModRM value.
constexpr uint8_t ModrmGetMod(uint8_t v) { return v >> 6; }

constexpr bool IsH66Prefix(uint8_t prefix) { return prefix == 0x66; }

constexpr bool IsRexPrefix(uint8_t prefix) { return (prefix >> 4) == 0b0100; }

constexpr bool HasSibByte(uint8_t mod_rm) {
  return ModrmGetMod(mod_rm) != kModRegToRegAddressing && (mod_rm & 0b111) == 0b100;
}

constexpr uint8_t DisplacementSize(uint8_t mod_rm, uint8_t sib) {
  switch (ModrmGetMod(mod_rm)) {
    case 0b00:
      if (HasSibByte(mod_rm) && (sib & kSibBaseMask) == kSibBaseNone) {
        return 4;
      } else {
        return 0;
      }
    case 0b01:
      return 1;
    case 0b10:
      return 4;
    default:
      FX_CHECK(false) << "Unexpected register-to-register instruction";
      __UNREACHABLE;
  }
}

constexpr uint8_t OperandSize(bool h66, bool rex_w, bool w, uint8_t default_operand_size) {
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

constexpr uint8_t ImmediateSize(bool h66, bool w, uint8_t default_operand_size) {
  if (!w) {
    return 1;
  } else if (!h66) {
    return default_operand_size;
  } else {
    return default_operand_size == 2 ? 4 : 2;
  }
}

constexpr uint8_t RegisterId(uint8_t mod_rm, bool rex_r) {
  return static_cast<uint8_t>(((mod_rm >> 3) & 0b111) + (rex_r ? 0b1000 : 0));
}

// From Intel Volume 2, Appendix B.1.4.1
//
// Registers 4-7 (typically referring to SP,BP,SI,DI) instead refer to the
// high byte registers (AH,CH,DH,BH) when using 1 byte registers and no rex
// prefix is provided.
constexpr inline bool IsHighByte(uint8_t size, bool rex) { return size == 1 && !rex; }

constexpr uint64_t* SelectRegister(zx_vcpu_state_t& vcpu_state, uint8_t register_id, uint8_t size,
                                   bool rex) {
  // From Intel Volume 2, Section 2.1.
  switch (register_id) {
    // From Intel Volume 2, Section 2.1.5.
    case 0:
      return &vcpu_state.rax;
    case 1:
      return &vcpu_state.rcx;
    case 2:
      return &vcpu_state.rdx;
    case 3:
      return &vcpu_state.rbx;
    case 4:
      if (IsHighByte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state.rsp;
    case 5:
      if (IsHighByte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state.rbp;
    case 6:
      if (IsHighByte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state.rsi;
    case 7:
      if (IsHighByte(size, rex)) {
        return nullptr;
      }
      return &vcpu_state.rdi;
    case 8:
      return &vcpu_state.r8;
    case 9:
      return &vcpu_state.r9;
    case 10:
      return &vcpu_state.r10;
    case 11:
      return &vcpu_state.r11;
    case 12:
      return &vcpu_state.r12;
    case 13:
      return &vcpu_state.r13;
    case 14:
      return &vcpu_state.r14;
    case 15:
      return &vcpu_state.r15;
    default:
      return nullptr;
  }
}

struct InstructionHeader {
  uint16_t opcode;
  uint8_t mod_rm;
  uint8_t sib;
};

zx::result<InstructionHeader> DeconstructHeader(InstructionSpan span) {
  if (span.empty()) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  InstructionHeader hdr{};
  switch (span[0]) {
    case 0x0f:
      if (span.size() < 3) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      // Use memcpy instead of casting, otherwise we may cause an unaligned
      // access, resulting in undefined behaviour.
      memcpy(&hdr.opcode, span.data(), sizeof(uint16_t));
      hdr.mod_rm = span[2];
      if (!HasSibByte(hdr.mod_rm)) {
        hdr.sib = 0;
      } else if (span.size() < 4) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      } else {
        hdr.sib = span[3];
      }
      return zx::ok(hdr);
    default:
      if (span.size() < 2) {
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      hdr.opcode = span[0];
      hdr.mod_rm = span[1];
      if (!HasSibByte(hdr.mod_rm)) {
        hdr.sib = 0;
      } else if (span.size() < 3) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      } else {
        hdr.sib = span[2];
      }
      return zx::ok(hdr);
  }
}

}  // namespace

// Decode an instruction used in a memory access to determine the register used
// as a source or destination. There's no need to decode memory operands because
// the faulting address is already known.
zx::result<Instruction> DecodeInstruction(InstructionSpan span, uint8_t default_operand_size,
                                          zx_vcpu_state_t& vcpu_state) {
  if (span.empty()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (span.size() > kMaxInstructionSize) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  if (default_operand_size != 2 && default_operand_size != 4) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Parse 66H prefix.
  bool h66 = IsH66Prefix(span[0]);
  if (h66) {
    if (span.size() == 1) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    span = span.subspan(1);
  }
  // Parse REX prefix.
  //
  // From Intel Volume 2, Appendix 2.2.1: Only one REX prefix is allowed per
  // instruction. If used, the REX prefix byte must immediately precede the
  // opcode byte or the escape opcode byte (0FH).
  bool rex = false;
  bool rex_r = false;
  bool rex_w = false;
  if (IsRexPrefix(span[0])) {
    rex = true;
    rex_r = span[0] & kRexRMask;
    rex_w = span[0] & kRexWMask;
    span = span.subspan(1);
  }
  // Technically this is valid, but no sane compiler should emit it.
  if (h66 && rex_w) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  auto hdr = DeconstructHeader(span);
  if (hdr.is_error()) {
    return hdr.take_error();
  }
  // Register-to-register addressing mode is not supported.
  if (ModrmGetMod(hdr->mod_rm) == kModRegToRegAddressing) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  const uint8_t sib_size = HasSibByte(hdr->mod_rm) ? 1 : 0;
  const uint8_t disp_size = DisplacementSize(hdr->mod_rm, hdr->sib);
  Instruction inst{};
  switch (hdr->opcode) {
    // Logical OR imm with r/m.
    // 1000 000w : mod 001 r/m : immediate data
    case 0x80:
    case 0x81: {
      const bool w = hdr->opcode & kWMask;
      const uint8_t imm_size = ImmediateSize(h66, w, default_operand_size);
      if (span.size() != sib_size + disp_size + imm_size + 2u) {
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      if (RegisterId(hdr->mod_rm, /*rex_r=*/false) != 1) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      inst.type = InstructionType::kLogicalOr;
      inst.access_size = OperandSize(h66, rex_w, w, default_operand_size);
      inst.imm = 0;
      inst.reg = nullptr;
      inst.flags = &vcpu_state.rflags;
      memcpy(&inst.imm, span.data() + sib_size + disp_size + 2, imm_size);
      return zx::ok(inst);
    }
    // Move r to r/m.
    // 1000 100w : mod reg r/m
    case 0x88:
    case 0x89: {
      if (span.size() != sib_size + disp_size + 2u) {
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      const bool w = hdr->opcode & kWMask;
      inst.type = InstructionType::kWrite;
      inst.access_size = OperandSize(h66, rex_w, w, default_operand_size);
      inst.imm = 0;
      inst.reg = SelectRegister(vcpu_state, RegisterId(hdr->mod_rm, rex_r), inst.access_size, rex);
      inst.flags = nullptr;
      if (inst.reg == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      return zx::ok(inst);
    }
    // Move r/m to r.
    // 1000 101w : mod reg r/m
    case 0x8a:
    case 0x8b: {
      if (span.size() != sib_size + disp_size + 2u) {
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      const bool w = hdr->opcode & kWMask;
      inst.type = InstructionType::kRead;
      inst.access_size = OperandSize(h66, rex_w, w, default_operand_size);
      inst.imm = 0;
      inst.reg = SelectRegister(vcpu_state, RegisterId(hdr->mod_rm, rex_r), inst.access_size, rex);
      inst.flags = nullptr;
      if (inst.reg == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      return zx::ok(inst);
    }
    // Move imm to r/m.
    // 1100 011w : mod 000 r/m : immediate data
    case 0xc6:
    case 0xc7: {
      const bool w = hdr->opcode & kWMask;
      const uint8_t imm_size = ImmediateSize(h66, w, default_operand_size);
      if (span.size() != sib_size + disp_size + imm_size + 2u) {
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      if ((hdr->mod_rm & kModRMRegMask) != 0) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      inst.type = InstructionType::kWrite;
      inst.access_size = OperandSize(h66, rex_w, w, default_operand_size);
      inst.imm = 0;
      inst.reg = nullptr;
      inst.flags = nullptr;
      memcpy(&inst.imm, span.data() + sib_size + disp_size + 2, imm_size);
      return zx::ok(inst);
    }
    // Move (16-bit) with zero-extend r/m to r.
    case 0xb70f:
      if (h66) {
        return zx::error(ZX_ERR_BAD_STATE);
      }
    // Move (8-bit) with zero-extend r/m to r.
    case 0xb60f: {
      if (span.size() != sib_size + disp_size + 3u) {
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      const bool w = hdr->opcode & (kWMask << 8);

      // We'll be operating with different sized operands due to the zero-
      // extend. The 'w' bit determines if we're reading 8 or 16 bits out of
      // memory while the h66/rex_w bits and default operand size are used to
      // select the destination register size.
      const uint8_t access_size = w ? 2 : 1;
      const uint8_t reg_size = OperandSize(h66, rex_w, true, default_operand_size);
      inst.type = InstructionType::kRead;
      inst.access_size = access_size;
      inst.imm = 0;
      inst.reg = SelectRegister(vcpu_state, RegisterId(hdr->mod_rm, rex_r), reg_size, rex);
      inst.flags = nullptr;
      if (inst.reg == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      return zx::ok(inst);
    }
    // Logical compare (8-bit) imm with r/m.
    case 0xf6:
      if (h66) {
        return zx::error(ZX_ERR_BAD_STATE);
      }
      if (span.size() != sib_size + disp_size + 3u) {
        return zx::error(ZX_ERR_OUT_OF_RANGE);
      }
      if ((hdr->mod_rm & kModRMRegMask) != 0) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      inst.type = InstructionType::kTest;
      inst.access_size = 1;
      inst.imm = 0;
      inst.reg = nullptr;
      inst.flags = &vcpu_state.rflags;
      memcpy(&inst.imm, span.data() + sib_size + disp_size + 2, 1);
      return zx::ok(inst);
    default:
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}
