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
spn_ring_init(struct spn_ring * const ring, uint32_t const size)
{
  assert(size >= 1);

  ring->size = size;
  ring->head = 0;
  ring->tail = 0;
  ring->rem  = size;
}

bool
spn_ring_is_empty(struct spn_ring const * const ring)
{
  return ring->rem == 0;
}

bool
spn_ring_is_full(struct spn_ring const * const ring)
{
  return ring->rem == ring->size;
}

bool
spn_ring_is_tail(struct spn_ring const * const ring, uint32_t const idx)
{
  return ring->tail == idx;
}

uint32_t
spn_ring_rem_nowrap(struct spn_ring const * const ring)
{
  return ring->size - ring->head;
}

uint32_t
spn_ring_acquire_1(struct spn_ring * const ring)
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
spn_ring_drop_1(struct spn_ring * const ring)
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
spn_ring_drop_n(struct spn_ring * const ring, uint32_t const n)
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
spn_ring_release_n(struct spn_ring * const ring, uint32_t const n)
{
  //
  // CAUTION: assumes conservation so no need to test before release
  //
  ring->rem += n;

  uint32_t const tail = ring->tail + n;

  ring->tail = (tail < ring->size) ? tail : tail - ring->size;
}

//
//
//

void
spn_next_init(struct spn_next * const next, uint32_t const size)
{
  next->size = size;
  next->head = 0;
}

uint32_t
spn_next_acquire_1(struct spn_next * const next)
{
  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  uint32_t const idx  = next->head;
  uint32_t const head = idx + 1;

  next->head = (head < next->size) ? head : 0;

  return idx;
}

void
spn_next_drop_n(struct spn_next * const next, uint32_t const n)
{
  //
  // CAUTION: this is unguarded so always test before acquiring
  //
  uint32_t const idx  = next->head;
  uint32_t const head = idx + n;

  next->head = (head < next->size) ? head : head - next->size;
}

//
//
//
