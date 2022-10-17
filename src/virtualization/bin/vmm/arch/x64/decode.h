// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_DECODE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_DECODE_H_

#include <zircon/syscalls/hypervisor.h>

#include "src/virtualization/bin/vmm/arch/x64/page_table.h"

#define X86_FLAGS_STATUS (FLAG_OF | FLAG_SF | FLAG_ZF | FLAG_PF | FLAG_RESERVED | FLAG_CF)

enum Flag {
  FLAG_OF = (1u << 11),
  FLAG_SF = (1u << 7),
  FLAG_ZF = (1u << 6),
  FLAG_PF = (1u << 2),
  FLAG_RESERVED = (1u << 1),
  FLAG_CF = (1u << 0)
};

enum class InstructionType : uint8_t {
  kRead,
  kWrite,
  kTest,
  kLogicalOr,
};

// Returns the flags that are assigned to the x86 flags register by an 8-bit
// TEST instruction for the given two operand values.
static inline uint16_t X86FlagsForTest8(uint8_t value1, uint8_t value2) {
  // TEST cannot set the overflow flag (bit 11).
  uint16_t ax_reg;
  __asm__ volatile(
      "testb %[i1], %[i2];"
      "lahf"  // Copies flags into the %ah register
      : "=a"(ax_reg)
      : [i1] "r"(value1), [i2] "r"(value2)
      : "cc");
  // Extract the value of the %ah register from the %ax register.
  return static_cast<uint16_t>(ax_reg >> 8);
}

// Instead of trying to define the x86 "or" operation in C (and, in particular,
// trying to calculate the various output flags), we simply run the "or"
// instruction directly and capture the flags.
template <typename T>
static inline uint16_t X86SimulateOr(T immediate, T& memory) {
  uint16_t ax_reg;
  __asm__(
      "or %[i1], %[i2];"
      "lahf"
      : "=a"(ax_reg), [i2] "+r"(memory)
      : [i1] "r"(immediate)
      : "cc");
  return static_cast<uint16_t>(ax_reg >> 8);
}

// Stores info from a decoded instruction.
struct Instruction {
  InstructionType type;
  uint8_t access_size;
  uint32_t imm;
  uint64_t* reg;
  uint64_t* flags;

  template <typename T>
  T Value() const {
    return static_cast<T>(reg != nullptr ? *reg : imm);
  }

  template <typename T>
  zx::result<> Read(T value) const {
    if (type != InstructionType::kRead || access_size != sizeof(T)) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    *reg = value;
    return zx::ok();
  }

  template <typename T>
  zx::result<> Write(T& value) const {
    if (type != InstructionType::kWrite || access_size != sizeof(T)) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    value = Value<T>();
    return zx::ok();
  }

  zx::result<> Test8(uint8_t inst_val, uint8_t value) const {
    if (type != InstructionType::kTest || access_size != 1u || Value<uint8_t>() != inst_val) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    *flags &= ~X86_FLAGS_STATUS;
    *flags |= X86FlagsForTest8(inst_val, value);
    return zx::ok();
  }

  template <typename T>
  zx::result<> Or(T inst_val, T& value) const {
    if (type != InstructionType::kLogicalOr || access_size != sizeof(T) || Value<T>() != inst_val) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    *flags &= ~X86_FLAGS_STATUS;
    *flags |= X86SimulateOr(inst_val, value);
    return zx::ok();
  }
};

zx::result<Instruction> DecodeInstruction(InstructionSpan span, uint8_t default_operand_size,
                                          zx_vcpu_state_t& vcpu_state);

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_DECODE_H_
