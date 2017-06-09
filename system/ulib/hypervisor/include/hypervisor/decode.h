// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

#define INST_MOV_READ   0u
#define INST_MOV_WRITE  1u
#define INST_TEST       2u

__BEGIN_CDECLS

typedef struct mx_guest_gpr mx_guest_gpr_t;

/* Stores info from a decoded instruction. */
typedef struct instruction {
    uint8_t type;
    uint8_t mem;
    uint32_t imm;
    uint64_t* reg;
} instruction_t;

mx_status_t decode_instruction(const uint8_t* inst_buf, uint32_t inst_len,
                               mx_guest_gpr_t* guest_gpr, instruction_t* inst);

__END_CDECLS
