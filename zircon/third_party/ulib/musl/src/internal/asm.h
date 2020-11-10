// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __has_feature
#define __has_feature(x) 0
#endif

// clang-format off
#define ENTRY(name)       \
  .globl name;            \
  .type name, %function;  \
  name:                   \
  .cfi_startproc
// clang-format on

#define END(name) \
  .cfi_endproc;   \
  .size name, .- name

// This copies the .type and .size info as well as the value.
#define ALIAS(old, new) \
  new = old;            \
  .globl new

#define WEAK_ALIAS(old, new) \
  new = old;                 \
  .weak new

// See __asan_weak_alias in asan_impl.h.
#if __has_feature(address_sanitizer)
#define ASAN_WEAK_ALIAS(name) WEAK_ALIAS(name, __asan_##name)
#else                          // !__has_feature(address_sanitizer)
#define ASAN_WEAK_ALIAS(name)  // Don't define __asan_##name.
#endif                         // __has_feature(address_sanitizer)

#ifdef __aarch64__  // clang-format off
.macro push_regs reg1, reg2
  stp \reg1, \reg2, [ sp, # - 16 ]!
  .cfi_adjust_cfa_offset 16
  .cfi_rel_offset \reg1, 0
  .cfi_rel_offset \reg2, 8
.endm

.macro pop_regs reg1, reg2
  ldp \reg1, \reg2, [sp], #16
  .cfi_adjust_cfa_offset - 16
  .cfi_same_value \reg1
  .cfi_same_value \reg2
.endm

.macro adr_global reg, symbol
#if __has_feature(hwaddress_sanitizer)
  adrp \reg, :pg_hi21_nc:\symbol
  movk \reg, #:prel_g3:\symbol+0x100000000
  add \reg, \reg, #:lo12:\symbol
#elif defined(__AARCH64_CMODEL_TINY__)
  adr \reg, \symbol
#else
  adrp \reg, \symbol
  add \reg, \reg, #:lo12:\symbol
#endif
.endm  // adr_global

#endif  // clang-format on

#ifdef __x86_64__  // clang-format off
.macro push_reg reg
  push \reg
  .cfi_adjust_cfa_offset 8
  .cfi_rel_offset \reg, 0
.endm

.macro pop_reg reg
  pop \reg
  .cfi_adjust_cfa_offset - 8
  .cfi_same_value \reg
.endm
#endif  // clang-format on
