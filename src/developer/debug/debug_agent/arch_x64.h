// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Do not include directly, use "arch.h".

#pragma once

#include <stdint.h>
#include <zircon/syscalls/debug.h>

namespace debug_agent {
namespace arch {

// The type that is large enough to hold the debug breakpoint CPU instruction.
using BreakInstructionType = uint8_t;

#define FLAG_VALUE(val, shift) ((val) & (shift))

// DR6 -------------------------------------------------------------------------

constexpr uint64_t kDR6B0 = (1 << 0);
constexpr uint64_t kDR6B1 = (1 << 1);
constexpr uint64_t kDR6B2 = (1 << 2);
constexpr uint64_t kDR6B3 = (1 << 3);
constexpr uint64_t kDR6BD = (1 << 13);
constexpr uint64_t kDR6BS = (1 << 14);
constexpr uint64_t kDR6BT = (1 << 15);

constexpr uint64_t kDR6Mask(0xffff0ff0ul);

// DR7 -------------------------------------------------------------------------

constexpr uint64_t kDR7L0 = (1 << 0);
constexpr uint64_t kDR7G0 = (1 << 1);
constexpr uint64_t kDR7L1 = (1 << 2);
constexpr uint64_t kDR7G1 = (1 << 3);
constexpr uint64_t kDR7L2 = (1 << 4);
constexpr uint64_t kDR7G2 = (1 << 5);
constexpr uint64_t kDR7L3 = (1 << 6);
constexpr uint64_t kDR7G3 = (1 << 7);
// Not used for now.
constexpr uint64_t kDR7LE = (1 << 8);
constexpr uint64_t kDR7GE = (1 << 9);
constexpr uint64_t kDR7GD = (1 << 13);
constexpr uint64_t kDR7RW0 = (1 << 16);
constexpr uint64_t kDR7LEN0 = (1 << 18);
constexpr uint64_t kDR7RW1 = (1 << 20);
constexpr uint64_t kDR7LEN1 = (1 << 22);
constexpr uint64_t kDR7RW2 = (1 << 24);
constexpr uint64_t kDR7LEN2 = (1 << 26);
constexpr uint64_t kDR7RW3 = (1 << 28);
constexpr uint64_t kDR7LEN3 = (1 << 30);

constexpr uint64_t kDR7Mask((1ul << 10) | kDR7LE | kDR7GE);

}  // namespace arch
}  // namespace debug_agent
