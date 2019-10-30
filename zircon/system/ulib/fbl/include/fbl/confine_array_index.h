// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_CONFINE_ARRAY_INDEX_H_
#define FBL_CONFINE_ARRAY_INDEX_H_

#include <zircon/assert.h>
#include <zircon/types.h>

namespace fbl {

// confine_array_index() bounds-checks and sanitizes an array index safely in the presence of
// speculative execution information leak bugs such as Spectre V1. confine_array_index() always
// returns a sanitized index, even in speculative-path execution.
//
// Callers need to combine confine_array_index with a conventional bounds check; the bounds
// check will return any necessary errors in the nonspeculative path, confine_array_index will
// confine indexes in the speculative path.
//
// Use:
// confine_array_index() returns |index|, if it is < size, or 0 if |index| is >= size.
//
// Example (may leak table1 contents):
//  1: int lookup3(size_t index) {
//  2:   if (index >= table1_size) {
//  3:     return -1;
//  4:   }
//  5:   size_t index2 = table1[index];
//  6:   return table2[index2];
//  7: }
//
// Converted:
//
//  1: int lookup3(size_t index) {
//  2:   if (index >= table1_size) {
//  3:     return -1;
//  4:   }
//  5:   size_t safe_index = confine_array_index(index, table1_size);
//  6:   size_t index2 = table1[safe_index];
//  7:   return table2[index2];
//  8: }

#ifdef __aarch64__
static inline size_t confine_array_index(size_t index, size_t size) {
  ZX_DEBUG_ASSERT(size > 0);
  size_t safe_index;

  // Use a conditional select and a CSDB barrier to enforce validation of |index|.
  // See "Cache Speculation Side-channels" whitepaper, section "Software Mitigation".
  // "" The combination of both a conditional select/conditional move and the new barrier are
  // sufficient to address this problem on ALL Arm implementations... ""
  asm(
    "cmp %1, %2\n"  // %1 holds the unsanitized index
    "csel %0, %1, xzr, lo\n"  // Select index or zero based on carry (%1 within range)
    "csdb\n"
  : "=r"(safe_index)
  : "r"(index), "r"(size)
  : "cc");

  return safe_index;
}
#endif

#ifdef __x86_64__
static inline size_t confine_array_index(size_t index, size_t size) {
  ZX_DEBUG_ASSERT(size > 0);

  // TODO(fxb/12540, fxb/33667): Convert to branchless sequence to reduce perfomance impact.
  if (index >= size) {
    return 0;
  }
  asm("lfence" : "+r"(index));
  return index;
}
#endif

}  // namespace fbl

#endif  // FBL_CONFINE_ARRAY_INDEX_H_
