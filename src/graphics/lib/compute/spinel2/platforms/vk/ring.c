// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "ring.h"

#include <assert.h>

//
// A barebones ring
//
void
spinel_ring_init(struct spinel_ring * const ring, uint32_t const size)
{
  assert(size >= 1);

  ring->size = size;
  ring->head = 0;
  ring->tail = 0;
  ring->rem  = size;
}

bool
spinel_ring_is_empty(struct spinel_ring const * const ring)
{
  return ring->rem == 0;
}

bool
spinel_ring_is_full(struct spinel_ring const * const ring)
{
  return ring->rem == ring->size;
}

bool
spinel_ring_is_tail(struct spinel_ring const * const ring, uint32_t const idx)
{
  return ring->tail == idx;
}

uint32_t
spinel_ring_dropped(struct spinel_ring const * const ring)
{
  return ring->size - ring->rem;
}

uint32_t
spinel_ring_head_nowrap(struct spinel_ring const * const ring)
{
  uint32_t const nowrap = ring->size - ring->head;

  return (ring->rem <= nowrap) ? ring->rem : nowrap;
}

uint32_t
spinel_ring_tail_nowrap(struct spinel_ring const * const ring)
{
  uint32_t const nowrap  = ring->size - ring->tail;
  uint32_t const dropped = spinel_ring_dropped(ring);

  return (dropped <= nowrap) ? dropped : nowrap;
}

uint32_t
spinel_ring_acquire_1(struct spinel_ring * const ring)
{
  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  ring->rem -= 1;

  uint32_t const idx  = ring->head;
  uint32_t const head = idx + 1;

  ring->head = (head < ring->size) ? head : 0;

  return idx;
}

void
spinel_ring_drop_1(struct spinel_ring * const ring)
{
  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  ring->rem -= 1;

  uint32_t const idx  = ring->head;
  uint32_t const head = idx + 1;

  ring->head = (head < ring->size) ? head : 0;
}

void
spinel_ring_drop_n(struct spinel_ring * const ring, uint32_t const n)
{
  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  ring->rem -= n;

  uint32_t const idx  = ring->head;
  uint32_t const head = idx + n;

  ring->head = (head < ring->size) ? head : head - ring->size;
}

void
spinel_ring_release_n(struct spinel_ring * const ring, uint32_t const n)
{
  //
  // CAUTION: assumes conservation so no need to test before release
  //
  ring->rem += n;

  uint32_t const tail = ring->tail + n;

  ring->tail = (tail < ring->size) ? tail : tail - ring->size;
}

//
// A subsidiary ring for when space is known to be implicitly
// available.
//
void
spinel_next_init(struct spinel_next * const next, uint32_t const size)
{
  next->size = size;
  next->head = 0;
}

uint32_t
spinel_next_acquire_1(struct spinel_next * const next)
{
  uint32_t const idx  = next->head;
  uint32_t const head = idx + 1;

  next->head = (head < next->size) ? head : 0;

  return idx;
}

uint32_t
spinel_next_acquire_2(struct spinel_next * const next)
{
  uint32_t const idx = next->head;

  if (idx + 1 < next->size)
    {
      uint32_t const head = idx + 2;

      next->head = (head < next->size) ? head : 0;

      return idx;
    }
  else  // we need two contiguous slots
    {
      next->head = 2;

      return 0;
    }
}

void
spinel_next_drop_n(struct spinel_next * const next, uint32_t const n)
{
  uint32_t const idx  = next->head;
  uint32_t const head = idx + n;

  next->head = (head < next->size) ? head : head - next->size;
}

//
//
//
