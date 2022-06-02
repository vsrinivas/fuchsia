// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <assert.h>
#include <stdlib.h>

//
//
//

#if defined(_MSC_VER) && !defined(__clang__)

#include <intrin.h>

#endif

//
//
//

bool
is_pow2_u32(uint32_t n)
{
  return n && !(n & (n - 1));
}

//
//
//

uint32_t
pow2_ru_u32(uint32_t n)
{
  assert(n <= 0x80000000U);

  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n++;

  return n;
}

//
//
//

uint32_t
pow2_rd_u32(uint32_t n)
{
  assert(n > 0);

  return 1u << msb_idx_u32(n);
}

//
// ASSUMES NON-ZERO
//

uint32_t
msb_idx_u32(uint32_t n)
{
  assert(n > 0);
#if defined(_MSC_VER) && !defined(__clang__)

  uint32_t index;

  _BitScanReverse((unsigned long *)&index, n);

  return index;

#elif defined(__GNUC__)

  return __builtin_clz(n) ^ 31;

#else

#error "No msb_index()"

#endif
}

//
// Invokes malloc()/calloc() and asserts if NULL.
//
void *
malloc_assert(size_t size)
{
  void * p = malloc(size);

  assert(p != NULL);

  return p;
}

void *
calloc_assert(size_t num, size_t size)
{
  void * p = calloc(num, size);

  assert(p != NULL);

  return p;
}

//
//
//
