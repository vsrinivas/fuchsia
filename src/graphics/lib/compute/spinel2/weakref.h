// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_WEAKREF_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_WEAKREF_H_

//
// A weakref is a small index and a large counter -- an epoch -- that
// can be *refuted* by comparing it to an externally held and
// increasing epoch.
//
// If the weakref's epoch and the external epoch match, then the
// weakref's index is considered valid.
//
// Note that if the weakref is subject to fuzzing or attack, the index
// should be clamped to a valid range.
//
// Epoch usage:
//
//   1. Initialize an epoch.  This is a counter with enough bits to
//      ensure it doesn't roll over.
//
//   2. Whenever an application-defined "epoch" has passed, increment
//      the epoch.
//
// Internal weakref usage:
//
//   1. If a weakref is invalid, then initialize it with an index and
//      the current epoch.
//
//   2. If a weakref is valid then its index is valid
//
// Example external weakref usage:
//
//   1. A transform or clip "stack" has a weakref for each entry in
//      the stack.
//
//   2. Whenever there is a new entry, invalidate the weakref.
//
//   3. Pass the entry and its associated weakref to the Spinel API.
//
//   4. If the weakref is determined to be valid, the entry will be
//      reused.  This improves decreases the amount of data copied or
//      loaded by the GPU and improves cache utilization... and saves
//      power.
//

#include "spinel/spinel_types.h"

//
// Two 32-bit dwords form a 64-bit counter with 32-bit alignment.
//
// A large fraction of the high bits are used as a counter.
//

typedef struct spinel_weakref_epoch
{
  uint32_t epoch[2];
} spinel_weakref_epoch_t;

//
//
//

void
spinel_weakref_epoch_init(spinel_weakref_epoch_t * epoch);

void
spinel_weakref_epoch_increment(spinel_weakref_epoch_t * epoch);

//
//
//

void
spinel_transform_weakrefs_init(spinel_transform_weakref_t *   weakrefs,
                               uint32_t const                 offset,
                               spinel_weakref_epoch_t const * epoch,
                               uint32_t const                 index);

bool
spinel_transform_weakrefs_get_index(spinel_transform_weakref_t const * weakrefs,
                                    uint32_t const                     offset,
                                    spinel_weakref_epoch_t const *     epoch,
                                    uint32_t *                         index);

//
//
//

void
spinel_clip_weakrefs_init(spinel_clip_weakref_t *        weakrefs,
                          uint32_t const                 offset,
                          spinel_weakref_epoch_t const * epoch,
                          uint32_t const                 index);

bool
spinel_clip_weakrefs_get_index(spinel_clip_weakref_t const *  weakrefs,
                               uint32_t const                 offset,
                               spinel_weakref_epoch_t const * epoch,
                               uint32_t *                     index);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_WEAKREF_H_
