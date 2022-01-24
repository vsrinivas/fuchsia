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
spinel_ring_init(struct spinel_ring * ring, uint32_t size)
{
  assert(size >= 1);

  ring->size = size;
  ring->head = 0;
  ring->tail = 0;
  ring->rem  = size;
}

bool
spinel_ring_is_empty(struct spinel_ring const * ring)
{
  return ring->rem == 0;
}

bool
spinel_ring_is_full(struct spinel_ring const * ring)
{
  return ring->rem == ring->size;
}

uint32_t
spinel_ring_dropped(struct spinel_ring const * ring)
{
  return ring->size - ring->rem;
}

uint32_t
spinel_ring_head_nowrap(struct spinel_ring const * ring)
{
  uint32_t const nowrap = ring->size - ring->head;

  return (ring->rem <= nowrap) ? ring->rem : nowrap;
}

uint32_t
spinel_ring_tail_nowrap(struct spinel_ring const * ring)
{
  uint32_t const nowrap  = ring->size - ring->tail;
  uint32_t const dropped = spinel_ring_dropped(ring);

  return (dropped <= nowrap) ? dropped : nowrap;
}

uint32_t
spinel_ring_acquire_1(struct spinel_ring * ring)
{
  assert(ring->rem >= 1);

  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  ring->rem -= 1;

  uint32_t const head     = ring->head;
  uint32_t const new_head = head + 1;

  ring->head = (new_head < ring->size) ? new_head : 0;

  return head;
}

void
spinel_ring_drop_1(struct spinel_ring * ring)
{
  assert(ring->rem >= 1);

  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  ring->rem -= 1;

  uint32_t const head     = ring->head;
  uint32_t const new_head = head + 1;

  ring->head = (new_head < ring->size) ? new_head : 0;
}

void
spinel_ring_drop_n(struct spinel_ring * ring, uint32_t n)
{
  assert(ring->rem >= n);

  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  ring->rem -= n;

  uint32_t const head     = ring->head;
  uint32_t const new_head = head + n;

  ring->head = (new_head < ring->size) ? new_head : new_head - ring->size;
}

void
spinel_ring_release_n(struct spinel_ring * ring, uint32_t n)
{
  assert(ring->rem + n <= ring->size);

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
spinel_next_init(struct spinel_next * next, uint32_t size)
{
  next->size = size;
  next->head = 0;
}

uint32_t
spinel_next_acquire_1(struct spinel_next * next)
{
  uint32_t const head     = next->head;
  uint32_t const new_head = head + 1;

  next->head = (new_head < next->size) ? new_head : 0;

  return head;
}

uint32_t
spinel_next_acquire_2(struct spinel_next * next, uint32_t * span)
{
  uint32_t const head = next->head;

  if (head + 1 < next->size)
    {
      uint32_t const new_head = head + 2;

      next->head = (new_head < next->size) ? new_head : 0;

      *span = 2;

      return head;
    }
  else  // we need two contiguous slots
    {
      next->head = 2;

      *span = 3;

      return 0;
    }
}

void
spinel_next_drop_n(struct spinel_next * next, uint32_t n)
{
  uint32_t const head     = next->head;
  uint32_t const new_head = head + n;

  next->head = (new_head < next->size) ? new_head : new_head - next->size;
}

//
//
//
