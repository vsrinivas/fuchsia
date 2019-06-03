// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "weakref.h"

#include "common/macros.h"

//
// WEAKREF
//

//
// clang-format off
//

#define SPN_WEAKREF_INDEX_BITS   16 // max bits for a weakref index
#define SPN_WEAKREF_INDEX_COUNT  (1 << SPN_WEAKREF_INDEX_BITS)

#define SPN_WEAKREF_INDEX_MASK   BITS_TO_MASK_MACRO(SPN_WEAKREF_INDEX_BITS)
#define SPN_WEAKREF_EPOCH_MASK   BITS_TO_MASK_AT_64_MACRO(64-SPN_WEAKREF_INDEX_BITS,SPN_WEAKREF_INDEX_BITS)

#define SPN_WEAKREF_EPOCH_ONE    BITS_TO_MASK_AT_64_MACRO(1,SPN_WEAKREF_INDEX_BITS)

//
// FIXME -- ASSUMES LITTLE-ENDIAN
//

union spn_weakref
{
  spn_weakref_t u64; // 64-bits containing refutation epoch and an index

  struct {
    uint32_t    index    :      SPN_WEAKREF_INDEX_BITS;
    uint32_t    epoch_lo : 32 - SPN_WEAKREF_INDEX_BITS;
    uint32_t    epoch_hi;
  };

  struct {
    uint64_t             :      SPN_WEAKREF_INDEX_BITS;
    uint64_t    epoch    : 64 - SPN_WEAKREF_INDEX_BITS;
  };
};

STATIC_ASSERT_MACRO((sizeof(spn_weakref_t) == sizeof(uint64_t)),
                    "sizeof(spn_weakref_t) != sizeof(uint64_t)");

STATIC_ASSERT_MACRO(sizeof(union spn_weakref) == sizeof(uint64_t),
                    "sizeof(union spn_weakref) != sizeof(uint64_t)");

//
// clang-format on
//

//
//
//

void
spn_weakref_epoch_init(spn_weakref_epoch_t * const epoch_p)
{
  *epoch_p = SPN_WEAKREF_EPOCH_ONE;
}

void
spn_weakref_epoch_bump(spn_weakref_epoch_t * const epoch_p)
{
  *epoch_p += SPN_WEAKREF_EPOCH_ONE;
}

//
//
//

#ifndef NDEBUG

static bool
spn_weakref_is_index_out_of_range(uint32_t const index)
{
  return index >= SPN_WEAKREF_INDEX_COUNT;
}

#endif

void
spn_weakref_init(spn_weakref_t * const weakref_p)
{
  *weakref_p = SPN_WEAKREF_INVALID;
}

void
spn_weakref_update(spn_weakref_t * const     weakref_p,
                   spn_weakref_epoch_t const epoch,
                   uint32_t const            index)
{
  assert(!spn_weakref_is_index_out_of_range(index));

  *weakref_p = epoch | index;
}

bool
spn_weakref_get_index(spn_weakref_t const * const weakref_p,
                      spn_weakref_epoch_t const   epoch,
                      uint32_t * const            idx_p)
{
  union spn_weakref const weakref = { .u64 = *weakref_p };

  if (((weakref.u64 ^ epoch) & SPN_WEAKREF_EPOCH_MASK) != 0UL)
    {
      return false;
    }

  *idx_p = weakref.index;

  return true;
}

//
//
//
