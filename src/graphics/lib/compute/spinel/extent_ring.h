// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXTENT_RING_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXTENT_RING_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

#include "allocator_host.h"

//
// The "ring" is a specialized extent designed to accumulate complete
// sequences of commands that are constructed by the host and executed
// on the device.
//
// Note that a sequence of commands is considered to be "complete"
// once a checkpoint has been invoked.
//
// Construction of paths and rasters depends on the checkpointing
// feature.
//
// Note that the ring no longer attempts to account for outstanding
// refcounts on the ring and its snaps.  Waiting for snaps to complete
// is a responsibility best handled elsewhere and up the stack.
//

struct spn_extent_ring_snap;

struct spn_extent_ring
{
  struct spn_extent_ring_snap * head;
  struct spn_extent_ring_snap * last;

  struct
  {
    uint32_t reads;   // number of reads
    uint32_t writes;  // number of writes
  } outer;

  struct
  {
    uint32_t reads;   // number of reads
    uint32_t writes;  // number of writes
  } inner;

  struct
  {
    uint32_t pow2;  // ring size must be pow2
    uint32_t mask;  // modulo is a mask because size is pow2
    uint32_t snap;  // max elements in a snapshot (not req'd to be pow2)
    uint32_t elem;  // size of element in bytes
  } size;
};

//
//
//

void
spn_extent_ring_init(struct spn_extent_ring * const ring,
                     uint32_t const                 size_pow2,
                     uint32_t const                 size_snap,
                     uint32_t const                 size_elem);

//
//
//

uint32_t
spn_extent_ring_rem(struct spn_extent_ring const * const ring);

bool
spn_extent_ring_is_full(struct spn_extent_ring const * const ring);

uint32_t
spn_extent_ring_wip_count(struct spn_extent_ring const * const ring);

uint32_t
spn_extent_ring_wip_rem(struct spn_extent_ring const * const ring);

bool
spn_extent_ring_wip_is_full(struct spn_extent_ring const * const ring);

uint32_t
spn_extent_ring_wip_index_inc(struct spn_extent_ring * const ring);

//
//
//

void
spn_extent_ring_checkpoint(struct spn_extent_ring * const ring);

//
// FIXME -- we can hide this implementation since it's dynamically
// allocated -- usually from temporary memory
//

struct spn_extent_ring_snap
{
  struct spn_extent_ring *      ring;  // parent ring
  struct spn_extent_ring_snap * next;  // next snap

  uint32_t reads;   // number of reads
  uint32_t writes;  // number of writes

  bool is_free;

  spn_subbuf_id_t id;  // id of host temp suballocation
};

//
// For now, all ring snaps allocations occur in "host temporary"
// memory.
//

struct spn_extent_ring_snap *
spn_extent_ring_snap_temp_alloc(struct spn_allocator_host_temp * const host_temp,
                                struct spn_device * const              device,
                                spn_result (*const wait)(struct spn_device * const device),
                                struct spn_extent_ring * const ring);

void
spn_extent_ring_snap_temp_free(struct spn_allocator_host_temp * const host_temp,
                               struct spn_extent_ring_snap * const    snap);

//
//
//

uint32_t
spn_extent_ring_snap_count(struct spn_extent_ring_snap const * const snap);

uint32_t
spn_extent_ring_snap_from(struct spn_extent_ring_snap const * const snap);

uint32_t
spn_extent_ring_snap_to(struct spn_extent_ring_snap const * const snap);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXTENT_RING_H_
