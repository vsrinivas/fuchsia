// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls/debug.h>

// This provides some utilities for testing that sets of register values
// are reported correctly.

#if defined(__x86_64__)
typedef mx_x86_64_general_regs_t mx_general_regs_t;
#define REG_PC rip
#elif defined(__aarch64__)
typedef mx_arm64_general_regs_t mx_general_regs_t;
#define REG_PC pc
#else
# error Unsupported architecture
#endif

// This initializes the register set with arbitrary test data.
void regs_fill_test_values(mx_general_regs_t* regs);

// This returns whether the two register sets' values are equal.
bool regs_expect_eq(mx_general_regs_t* regs1, mx_general_regs_t* regs2);

// This function sets the registers to the state specified by |regs| and
// then spins, executing a single-instruction infinite loop whose address
// is |spin_address|.
void spin_with_regs(mx_general_regs_t* regs);
void spin_with_regs_spin_address(void);
