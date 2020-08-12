// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_CONDITIONAL_SELECT_NOSPEC_H_
#define FBL_CONDITIONAL_SELECT_NOSPEC_H_

#include <sys/types.h>

namespace fbl {

// conditional_select_nospec_*() returns one of its two integral arguments based on whether its
// |predicate| argument is true or false, like a ternary expression. It uses branchless
// sequences on every architecture and is immune to speculative execution information leak bugs
// such as Spectre V1.
//
// Use:
// conditional_select_nospec_eq() returns |a| if |x| == |y|, |b| otherwise.
// conditional_select_nospec_lt() returns |a| if |x| < |y|, |b| otherwise.
// It does so even in wrong-path speculative executions.
//
// Example (susceptible to bounds check bypass / Spectre V1):
// 1: Thing* lookup4(size_t index, uint64_t stamp) {
// 2:   size_t safe_index = index & 0xff;
// 4:   Thing* const thing = &table_[safe_index];
// 5:   return (thing->stamp == stamp) ? thing : nullptr;
// 6: }
//
// Hostile code can cause the ternary expression at line 5 to return |thing| even if
// thing->stamp != stamp in wrong-path speculative execution. If dependent code uses values
// derived from |thing| to look up values in other data structures, the lookup cache side effects
// may be observable and allow hostile code to infer values in a structure that it should not
// have had access to.
//
// Converted:
// 1: Thing* lookup4(size_t index, uint64_t stamp) {
// 2:   size_t safe_index = index & 0xff;
// 3:   Thing* const thing = &table_[safe_index];
// 4:   return fbl::conditional_select_nospec_eq(thing->stamp, stamp, thing, 0);
// 5: }
//
// To avoid Spectre V1-style attacks, a caller must also avoid branching on the return value of
// conditional_select_nospec(); it may do so by using a safe object for |b|; it may also be able
// to use nullptr if AARCH64 PAN / x86-64 SMAP are available.
#if __aarch64__
// (x == y) ? a : b
static inline size_t conditional_select_nospec_eq(size_t x, size_t y, size_t a, size_t b) {
  size_t select;

  // Use a conditional select and a CSDB barrier to enforce selection.
  // See "Cache Speculation Side-channels" whitepaper, section "Software Mitigation".
  // "" The combination of both a conditional select/conditional move and the new barrier are
  // sufficient to address this problem on ALL Arm implementations... ""
  asm("cmp %1, %2\n"
      "csel %0, %3, %4, eq\n"
      "csdb\n"
      : "=r"(select)
      : "r"(x), "r"(y), "r"(a), "r"(b)
      : "cc");

  return select;
}

// (x < y) ? a : b
static inline size_t conditional_select_nospec_lt(size_t x, size_t y, size_t a, size_t b) {
  size_t select;

  asm("cmp %1, %2\n"
      "csel %0, %3, %4, lo\n"
      "csdb\n"
      : "=r"(select)
      : "r"(x), "r"(y), "r"(a), "r"(b)
      : "cc");

  return select;
}

#elif __x86_64__

// (x == y) ? a : b
static inline size_t conditional_select_nospec_eq(size_t x, size_t y, size_t a, size_t b) {
  size_t select = a;

  // Use a test / conditional move to select between |a| and |b|.
  // The conditional move has a data dependency on the result of a TEST instruction
  // and cannot execute until the comparison is resolved.
  // See "Software Techniques for Managing Speculation on AMD Processors", Mitigation V1-2.
  // See "Analyzing potential bounds check bypass vulnerabilities", Revision 002,
  //   Section 5.2 Bounds clipping
  __asm__(
      "cmp %1, %2\n"
      "cmovnz %3, %0\n"
      : "+r"(select)
      : "r"(x), "r"(y), "r"(b)
      : "cc");

  return select;
}

// (x < y) ? a : b
static inline size_t conditional_select_nospec_lt(size_t x, size_t y, size_t a, size_t b) {
  size_t select = a;

  __asm__(
      "cmp %2, %1\n"
      "cmovae %3, %0\n"
      : "+r"(select)
      : "r"(x), "r"(y), "r"(b)
      : "cc");

  return select;
}

#else
#error "Provide implementations of conditional_select for your ARCH here"
#endif

}  // namespace fbl

#endif  // FBL_CONDITIONAL_SELECT_NOSPEC_H_
