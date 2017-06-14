// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#define X86_FLAGS_STATUS    ((1u << 11  /* OF */) | \
                             (1u << 7   /* SF */) | \
                             (1u << 6   /* ZF */) | \
                             (1u << 2   /* PF */) | \
                             (1u << 1   /* Reserved (must be 1) */) | \
                             (1u << 0   /* CF */))

#define INST_MOV_READ       0u
#define INST_MOV_WRITE      1u
#define INST_TEST           2u

__BEGIN_CDECLS

typedef struct mx_guest_gpr mx_guest_gpr_t;

/* Stores info from a decoded instruction. */
typedef struct instruction {
    uint8_t type;
    uint8_t mem;
    uint32_t imm;
    uint64_t* reg;
    uint16_t* flags;
} instruction_t;

mx_status_t inst_decode(const uint8_t* inst_buf, uint32_t inst_len, mx_guest_gpr_t* guest_gpr,
                        instruction_t* inst);

#define DEFINE_INST_VAL(size) \
inline uint ## size ## _t inst_val ## size(const instruction_t* inst) { \
    return inst->reg != NULL ? *inst->reg : inst->imm; \
}
DEFINE_INST_VAL(32);
DEFINE_INST_VAL(16);
DEFINE_INST_VAL(8);
#undef DEFINE_INST_VAL

#define DEFINE_INST_READ(size) \
inline mx_status_t inst_read ## size(const instruction_t* inst, uint ## size ## _t value) { \
    if (inst->type != INST_MOV_READ || inst->mem != (size / 8)) \
        return MX_ERR_NOT_SUPPORTED; \
    *inst->reg = value; \
    return MX_OK; \
}
DEFINE_INST_READ(32);
DEFINE_INST_READ(16);
DEFINE_INST_READ(8);
#undef DEFINE_INST_READ

#define DEFINE_INST_WRITE(size) \
inline mx_status_t inst_write ## size(const instruction_t* inst, uint ## size ## _t* value) { \
    if (inst->type != INST_MOV_WRITE || inst->mem != (size / 8)) \
        return MX_ERR_NOT_SUPPORTED; \
    *value = inst_val ## size(inst); \
    return MX_OK; \
}
DEFINE_INST_WRITE(32);
DEFINE_INST_WRITE(16);
#undef DEFINE_INST_WRITE

#define DEFINE_INST_RW(size) \
inline mx_status_t inst_rw ## size(const instruction_t* inst, uint ## size ## _t* value) { \
    if (inst->type == INST_MOV_READ) { \
        return inst_read ## size(inst, *value); \
    } else if (inst->type == INST_MOV_WRITE) { \
        return inst_write ## size(inst, value); \
    } else { \
        return MX_ERR_NOT_SUPPORTED; \
    } \
}
DEFINE_INST_RW(32);
DEFINE_INST_RW(16);
#undef DEFINE_INST_RW

inline mx_status_t inst_test8(const instruction_t* inst, uint8_t inst_val, uint8_t value) {
    if (inst->type != INST_TEST || inst->mem != 1u || inst_val8(inst) != inst_val)
        return MX_ERR_NOT_SUPPORTED;
#if __x86_64__
    uint16_t flags;
    __asm__ volatile (
        "testb %[i1], %[i2];"
        "pushfw;"
        "popw %[flags];"
        : [flags] "=r"(flags)
        : [i1] "r"(inst_val), [i2] "r"(value)
        : "cc");

    *inst->flags &= ~X86_FLAGS_STATUS;
    *inst->flags |= (flags & X86_FLAGS_STATUS);
    return MX_OK;
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

__END_CDECLS
