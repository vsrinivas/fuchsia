// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_ARM64_CONTEXT_H_
#define PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_ARM64_CONTEXT_H_

// Offset of all saved registers.
#define X19_O 0
#define X20_O 8
#define X21_O 16
#define X22_O 24
#define X23_O 32
#define X24_O 40
#define X25_O 48
#define X26_O 56
#define X27_O 64
#define X28_O 72
#define X29_O 80
#define X30_O 88
#define SP_O 96
#define D8_O 104
#define D9_O 112
#define D10_O 120
#define D11_O 128
#define D12_O 136
#define D13_O 144
#define D14_O 152
#define D15_O 160
#define X0_O 168
#define UNSAFE_SP_O 176

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

namespace context {

enum Register {
  REG_X19 = 0,
  REG_X20,
  REG_X21,
  REG_X22,
  REG_X23,
  REG_X24,
  REG_X25,
  REG_X26,
  REG_X27,
  REG_X28,
  REG_X29,
  REG_X30,
  REG_SP,
  REG_D8,
  REG_D9,
  REG_D10,
  REG_D11,
  REG_D12,
  REG_D13,
  REG_D14,
  REG_D15,
  REG_X0,
  REG_UNSAFE_SP,
  NUM_REGISTERS,

  // Special registers.
  REG_ARG0 = REG_X0,
  REG_LR = REG_X30,
};

constexpr size_t kAdditionalStackAlignment = 0;

struct InternalContext {
  uint64_t registers[NUM_REGISTERS];
};

#define ASSERT_REGISTER_OFFSET(REG)                                 \
  static_assert(offsetof(struct InternalContext,                    \
                         registers[context::REG_##REG]) == REG##_O, \
                "offset is incorrect")

ASSERT_REGISTER_OFFSET(X19);
ASSERT_REGISTER_OFFSET(X20);
ASSERT_REGISTER_OFFSET(X21);
ASSERT_REGISTER_OFFSET(X22);
ASSERT_REGISTER_OFFSET(X23);
ASSERT_REGISTER_OFFSET(X24);
ASSERT_REGISTER_OFFSET(X25);
ASSERT_REGISTER_OFFSET(X26);
ASSERT_REGISTER_OFFSET(X27);
ASSERT_REGISTER_OFFSET(X28);
ASSERT_REGISTER_OFFSET(X29);
ASSERT_REGISTER_OFFSET(X30);
ASSERT_REGISTER_OFFSET(SP);
ASSERT_REGISTER_OFFSET(D8);
ASSERT_REGISTER_OFFSET(D9);
ASSERT_REGISTER_OFFSET(D10);
ASSERT_REGISTER_OFFSET(D11);
ASSERT_REGISTER_OFFSET(D12);
ASSERT_REGISTER_OFFSET(D13);
ASSERT_REGISTER_OFFSET(D14);
ASSERT_REGISTER_OFFSET(D15);
ASSERT_REGISTER_OFFSET(X0);
ASSERT_REGISTER_OFFSET(UNSAFE_SP);

}  // namespace context

#endif  // __ASSEMBLER__

#endif  // PERIDOT_BIN_LEDGER_COROUTINE_CONTEXT_ARM64_CONTEXT_H_
