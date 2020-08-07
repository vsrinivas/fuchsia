// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// These get mangled so the raw pointer values don't leak into the heap.
#define JB_PC 0
#define JB_SP 1
#define JB_FP 2
#define JB_USP 3
#define JB_MANGLE_COUNT (4 + JB_ARCH_MANGLE_COUNT)

#ifdef __x86_64__

// Other callee-saves registers.
#define JB_RBX (JB_MANGLE_COUNT + 0)
#define JB_R12 (JB_MANGLE_COUNT + 1)
#define JB_R13 (JB_MANGLE_COUNT + 2)
#define JB_R14 (JB_MANGLE_COUNT + 3)
#define JB_R15 (JB_MANGLE_COUNT + 4)
#define JB_COUNT (JB_MANGLE_COUNT + 5)

#define JB_ARCH_MANGLE_COUNT 0

#elif defined(__aarch64__)

// The shadow call stack pointer (x18) is also mangled.
#define JB_ARCH_MANGLE_COUNT 1

// Callee-saves registers are [x19,x28] and [d8,d15].
#define JB_X(n) (JB_MANGLE_COUNT + n - 19)
#define JB_D(n) (JB_X(29) + n - 8)
#define JB_COUNT JB_D(16)

#elif defined(__riscv)

// The shadow call stack pointer (x18) is also mangled.
#define JB_ARCH_MANGLE_COUNT 1

// Callee-saves registers are [x19,x28] and [d8,d15].
#define JB_X(n) (JB_MANGLE_COUNT + n - 19)
#define JB_D(n) (JB_X(29) + n - 8)
#define JB_COUNT JB_D(16)

#else

#error what architecture?

#endif

#ifndef __ASSEMBLER__

#include <stdint.h>

extern struct setjmp_manglers {
  uintptr_t mangle[JB_MANGLE_COUNT];
} __setjmp_manglers __attribute__((visibility("hidden")));

#endif
