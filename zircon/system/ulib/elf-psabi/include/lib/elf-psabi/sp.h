// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

// Given the base and size of the stack block, compute the appropriate
// initial SP value for an initial thread according to the C calling
// convention for the machine.
static inline uintptr_t compute_initial_stack_pointer(uintptr_t base, size_t size) {
  // Assume stack grows down.
  uintptr_t sp = base + size;

  // The x86-64 and AArch64 ABIs require 16-byte alignment.
  // The 32-bit ARM ABI only requires 8-byte alignment, but
  // 16-byte alignment is preferable for NEON so use it there too.
  sp &= -16;

#ifdef __x86_64__
  // The x86-64 ABI requires %rsp % 16 = 8 on entry.  The zero word
  // at (%rsp) serves as the return address for the outermost frame.
  sp -= 8;
#elif defined(__arm__) || defined(__aarch64__)
  // The ARMv7 and ARMv8 ABIs both just require that SP be aligned.
#elif defined(__riscv)
#else
#error what machine?
#endif

  return sp;
}
