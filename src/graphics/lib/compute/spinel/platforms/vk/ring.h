// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_RING_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_RING_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
// Simple ring
//

struct spn_ring
{
  uint32_t size;
  uint32_t head;
  uint32_t tail;
  uint32_t rem;
};

//
// Simpler than a ring when slot allocation is implicitly managed
// elsewhere
//

struct spn_next
{
  uint32_t size;
  uint32_t head;
};

//
// Note that this ring implementation leaves one slot empty
//

void
spn_ring_init(struct spn_ring * const ring, uint32_t const size);

bool
spn_ring_is_empty(struct spn_ring const * const ring);

bool
spn_ring_is_full(struct spn_ring const * const ring);

bool
spn_ring_is_tail(struct spn_ring const * const ring, uint32_t const idx);

uint32_t
spn_ring_rem_nowrap(struct spn_ring const * const ring);

uint32_t
spn_ring_acquire_1(struct spn_ring * const ring);

void
spn_ring_drop_1(struct spn_ring * const ring);

void
spn_ring_drop_n(struct spn_ring * const ring, uint32_t const n);

void
spn_ring_release_n(struct spn_ring * const ring, uint32_t const n);

//
//
//

void
spn_next_init(struct spn_next * const next, uint32_t const size);

uint32_t
spn_next_acquire_1(struct spn_next * const next);

void
spn_next_drop_n(struct spn_next * const next, uint32_t const n);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_RING_H_
