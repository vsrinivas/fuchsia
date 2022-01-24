// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_RING_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_RING_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
// Simple ring
//

struct spinel_ring
{
  uint32_t size;
  uint32_t head;
  uint32_t tail;
  uint32_t rem;
};

//
// A subsidiary ring for when space is known to be implicitly
// available.
//

struct spinel_next
{
  uint32_t size;
  uint32_t head;
};

//
// Note that this ring implementation leaves one slot empty
//

void
spinel_ring_init(struct spinel_ring * ring, uint32_t size);

bool
spinel_ring_is_empty(struct spinel_ring const * ring);

bool
spinel_ring_is_full(struct spinel_ring const * ring);

uint32_t
spinel_ring_dropped(struct spinel_ring const * ring);

uint32_t
spinel_ring_head_nowrap(struct spinel_ring const * ring);

uint32_t
spinel_ring_tail_nowrap(struct spinel_ring const * ring);

uint32_t
spinel_ring_acquire_1(struct spinel_ring * ring);

void
spinel_ring_drop_1(struct spinel_ring * ring);

void
spinel_ring_drop_n(struct spinel_ring * ring, uint32_t n);

void
spinel_ring_release_n(struct spinel_ring * ring, uint32_t n);

//
//
//

void
spinel_next_init(struct spinel_next * next, uint32_t size);

uint32_t
spinel_next_acquire_1(struct spinel_next * next);

uint32_t
spinel_next_acquire_2(struct spinel_next * next, uint32_t * span);

void
spinel_next_drop_n(struct spinel_next * next, uint32_t n);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_RING_H_
