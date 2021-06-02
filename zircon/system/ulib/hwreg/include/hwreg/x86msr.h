// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWREG_X86MSR_H_
#define HWREG_X86MSR_H_

#include "internal.h"

#if !defined(__x86_64__) && !defined(__i386__)
#error "<hwreg/x86msr.h> is for x86 only"
#endif

namespace hwreg {

// This can be passed to ReadFrom and WriteTo methods.
// The RegisterAddr object holds the whole MSR number.

struct X86MsrIo {
  template <typename IntType>
  void Write(IntType value, uint32_t msr) const {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    uint64_t v = static_cast<uint64_t>(value);
    // The high-order 32 bits of each register are ignored so they need not be
    // cleared.  uintptr_t is 32 bits on x86-32 so that values will match the
    // register size, but 64 bits on x86-64 so that the compiler doesn't think
    // it needs to add an instruction to clear the high bits.
    uintptr_t lo = static_cast<uintptr_t>(v);
    uintptr_t hi = static_cast<uintptr_t>(v >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
  }

  template <typename IntType>
  IntType Read(uint32_t msr) const {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return static_cast<IntType>((static_cast<uint64_t>(hi) << 32) | lo);
  }
};

}  // namespace hwreg

#endif  // HWREG_X86MSR_H_
