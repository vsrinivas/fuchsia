// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_DECODE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_DECODE_H_

#include <zircon/types.h>

// clang-format off

#define FLAG_OF             (1u << 11)
#define FLAG_SF             (1u << 7)
#define FLAG_ZF             (1u << 6)
#define FLAG_PF             (1u << 2)
#define FLAG_RESERVED       (1u << 1)
#define FLAG_CF             (1u << 0)

#define X86_FLAGS_STATUS    (FLAG_OF | FLAG_SF | FLAG_ZF | FLAG_PF | FLAG_RESERVED | FLAG_CF)

#define INST_MOV_READ       0u
#define INST_MOV_WRITE      1u
#define INST_TEST           2u
#define INST_OR             3u

// clang-format on

typedef struct zx_vcpu_state zx_vcpu_state_t;

// Stores info from a decoded instruction.
struct Instruction {
  uint8_t type;
  uint8_t access_size;
  uint32_t imm;
  uint64_t* reg;
  uint64_t* flags;
};

zx_status_t inst_decode(const uint8_t* inst_buf, uint32_t inst_len, uint8_t default_operand_size,
                        zx_vcpu_state_t* vcpu_state, Instruction* inst);

template <typename T>
static inline T get_inst_val(const Instruction* inst) {
  return static_cast<T>(inst->reg != nullptr ? *inst->reg : inst->imm);
}

template <typename T>
static inline zx_status_t inst_read(const Instruction* inst, T value) {
  if (inst->type != INST_MOV_READ || inst->access_size != sizeof(T)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *inst->reg = value;
  return ZX_OK;
}

template <typename T>
static inline zx_status_t inst_write(const Instruction* inst, T* value) {
  if (inst->type != INST_MOV_WRITE || inst->access_size != sizeof(T)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *value = get_inst_val<T>(inst);
  return ZX_OK;
}

// Returns the flags that are assigned to the x86 flags register by an
// 8-bit TEST instruction for the given two operand values.
static inline uint16_t x86_flags_for_test8(uint8_t value1, uint8_t value2) {
  // TEST cannot set the overflow flag (bit 11).
  uint16_t ax_reg;
  __asm__ volatile(
      "testb %[i1], %[i2];"
      "lahf"  // Copies flags into the %ah register
      : "=a"(ax_reg)
      : [i1] "r"(value1), [i2] "r"(value2)
      : "cc");
  // Extract the value of the %ah register from the %ax register.
  return (uint16_t)(ax_reg >> 8);
}

static inline zx_status_t inst_test8(const Instruction* inst, uint8_t inst_val, uint8_t value) {
  if (inst->type != INST_TEST || inst->access_size != 1u ||
      get_inst_val<uint8_t>(inst) != inst_val) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *inst->flags &= ~X86_FLAGS_STATUS;
  *inst->flags |= x86_flags_for_test8(inst_val, value);
  return ZX_OK;
}

// Instead of trying to define the x86 "or" operation in C (and, in particular, trying to calculate
// the various output flags), we simply run the "or" instruction directly and capture the flags.
template <typename T>
static inline uint16_t x86_simulate_or(T immediate, T* memory) {
  uint16_t ax_reg;
  __asm__(
      "or %[i1], %[i2];"
      "lahf"
      : "=a"(ax_reg), [i2] "+r"(*memory)
      : [i1] "r"(immediate)
      : "cc");
  return static_cast<uint16_t>(ax_reg >> 8);
}

template <typename T>
static inline zx_status_t inst_or(const Instruction* inst, T inst_val, T* value) {
  if (inst->type != INST_OR || inst->access_size != sizeof(T) ||
      get_inst_val<T>(inst) != inst_val) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *inst->flags &= ~X86_FLAGS_STATUS;
  *inst->flags |= x86_simulate_or(inst_val, value);
  return ZX_OK;
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_DECODE_H_
