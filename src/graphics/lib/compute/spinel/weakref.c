// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// A weakref is initialized with an epoch number and an index.
//
// The epoch number is assumed to never roll over and therefore be
// unique for the life of the Spinel context.
//
// Details:
//
//         V1                   V2*
//  0              63    0              63
//  | INDEX | EPOCH |    | INDEX | EPOCH |
//  +-------+-------+    +-------+-------+
//  |  ~13  |  ~51  |    |   8   |  56   |
//
// (* V2 forthcoming and uses only 8 bits for .index )
//
// Frequency of epoch increments determines how long it takes to a
// fatal rollover:
//
//       13:51 bits               8:56 bits
//  +------+-----------+    +------+------------+
//  |  Hz  |   Years   |    |  Hz  |   Years    |
//  +------+-----------+    +------+------------+
//  |  60  | 1,189,279 |    |  60  | 38,056,935 |
//  | 1000 |    71,356 |    | 1000 |  2,283,416 |
//  | 5000 |    14,271 |    | 5000 |    456,683 |
//
// Epochs are incremented in the path and raster builder ring buffers
// and probably can't be updated faster than a 1000 Hz.
//

#include "weakref.h"

#include <stdlib.h>

#include "common/macros.h"
#include "core_c.h"

//
// Weakref bit allocation
//
// clang-format off
#define SPN_WEAKREF_INDEX_BITS     13  // max bits for a weakref index
#define SPN_WEAKREF_INDEX_COUNT    (1 << SPN_WEAKREF_INDEX_BITS)

#define SPN_WEAKREF_LO_INDEX_MASK  BITS_TO_MASK_MACRO(SPN_WEAKREF_INDEX_BITS)

#define SPN_WEAKREF_EPOCH_LO_MASK  BITS_TO_MASK_AT_MACRO(32 - SPN_WEAKREF_INDEX_BITS, SPN_WEAKREF_INDEX_BITS)
#define SPN_WEAKREF_EPOCH_LO_ONE   SPN_WEAKREF_INDEX_COUNT
// clang-format on

//
// Compile-time guards listed here:
//

STATIC_ASSERT_MACRO_1(SPN_WEAKREF_INDEX_BITS >= SPN_TTRK_HI_BITS_COHORT);  // TTRK.RASTER_COHORT_ID

//
// Epoch starts at { .epoch = 0 }
//
//  0              63
//  |  N/A  | EPOCH |
//  +-------+-------+
//  |  ~13  |  ~51  |
//

void
spinel_weakref_epoch_init(spinel_weakref_epoch_t * epoch)
{
  *epoch = (spinel_weakref_epoch_t){ 0, 0 };
}

//
// Note that using multiprecision arithmetic intrinsics generates
// worse code because we're only incrementing by 1.
//

void
spinel_weakref_epoch_increment(spinel_weakref_epoch_t * epoch)
{
  epoch->epoch[0] += SPN_WEAKREF_EPOCH_LO_ONE;

  if (epoch->epoch[0] == 0)
    {
      epoch->epoch[1] += 1;
    }
}

//
//
//

static void
spinel_weakref_init(uint32_t weakref[2], spinel_weakref_epoch_t const * epoch, uint32_t const index)
{
  assert(index < SPN_WEAKREF_INDEX_COUNT);

  weakref[0] = epoch->epoch[0] | index;
  weakref[1] = epoch->epoch[1];
}

static bool
spinel_weakref_get_index(uint32_t const                 weakref[2],
                         spinel_weakref_epoch_t const * epoch,
                         uint32_t *                     index)
{
  // test lo
  if (((weakref[0] ^ epoch->epoch[0]) & SPN_WEAKREF_EPOCH_LO_MASK) != 0)
    {
      return false;
    }

  // test hi
  if (weakref[1] != epoch->epoch[1])
    {
      return false;
    }

  // weakref wasn't refuted
  *index = weakref[0] & SPN_WEAKREF_LO_INDEX_MASK;

  return true;
}

//
//
//

void
spinel_transform_weakrefs_init(spinel_transform_weakref_t *   weakrefs,
                               uint32_t const                 offset,
                               spinel_weakref_epoch_t const * epoch,
                               uint32_t const                 index)
{
  if (weakrefs == NULL)
    return;

  spinel_transform_weakref_t * weakref = weakrefs + offset;

  spinel_weakref_init(weakref->weakref, epoch, index);
}

bool
spinel_transform_weakrefs_get_index(spinel_transform_weakref_t const * weakrefs,
                                    uint32_t const                     offset,
                                    spinel_weakref_epoch_t const *     epoch,
                                    uint32_t *                         index)
{
  if (weakrefs == NULL)
    return false;

  spinel_transform_weakref_t const * weakref = weakrefs + offset;

  return spinel_weakref_get_index(weakref->weakref, epoch, index);
}

//
//
//

void
spinel_clip_weakrefs_init(spinel_clip_weakref_t *        weakrefs,
                          uint32_t const                 offset,
                          spinel_weakref_epoch_t const * epoch,
                          uint32_t const                 index)
{
  if (weakrefs == NULL)
    return;

  spinel_clip_weakref_t * weakref = weakrefs + offset;

  spinel_weakref_init(weakref->weakref, epoch, index);
}

bool
spinel_clip_weakrefs_get_index(spinel_clip_weakref_t const *  weakrefs,
                               uint32_t const                 offset,
                               spinel_weakref_epoch_t const * epoch,
                               uint32_t *                     index)
{
  if (weakrefs == NULL)
    return false;

  spinel_clip_weakref_t const * weakref = weakrefs + offset;

  return spinel_weakref_get_index(weakref->weakref, epoch, index);
}

//
//
//
