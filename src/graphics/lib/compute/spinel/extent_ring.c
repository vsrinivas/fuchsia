// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "extent_ring.h"
#include "common/macros.h"

//
//
//

void
spn_extent_ring_init(struct spn_extent_ring * const ring,
                     uint32_t                 const size_pow2,
                     uint32_t                 const size_snap,
                     uint32_t                 const size_elem)
{
  ring->head         = NULL;
  ring->last         = NULL;

  ring->outer.reads  = 0;
  ring->outer.writes = 0;

  ring->inner.reads  = 0;
  ring->inner.writes = 0;

  // FIXME -- assert size is pow2 -- either here or statically in the config

  ring->size.pow2    = size_pow2;
  ring->size.mask    = size_pow2 - 1;
  ring->size.snap    = size_snap;
  ring->size.elem    = size_elem;
}

//
//
//

uint32_t
spn_extent_ring_rem(struct spn_extent_ring const * const ring)
{
  return ring->size.pow2 - (ring->outer.writes - ring->outer.reads);
}

bool
spn_extent_ring_is_full(struct spn_extent_ring const * const ring)
{
  return (ring->outer.writes - ring->outer.reads) == ring->size.pow2;
}

uint32_t
spn_extent_ring_wip_count(struct spn_extent_ring const * const ring)
{
  return ring->outer.writes - ring->inner.reads;
}

uint32_t
spn_extent_ring_wip_rem(struct spn_extent_ring const * const ring)
{
  return MIN_MACRO(uint32_t,spn_extent_ring_rem(ring),ring->size.snap) - spn_extent_ring_wip_count(ring);
}

bool
spn_extent_ring_wip_is_full(struct spn_extent_ring const * const ring)
{
  return spn_extent_ring_wip_count(ring) == MIN_MACRO(uint32_t,spn_extent_ring_rem(ring),ring->size.snap);
}

uint32_t
spn_extent_ring_wip_index_inc(struct spn_extent_ring * const ring)
{
  return ring->outer.writes++ & ring->size.mask;
}

//
//
//

void
spn_extent_ring_checkpoint(struct spn_extent_ring * const ring)
{
  ring->inner.writes = ring->outer.writes;
}

//
//
//

struct spn_extent_ring_snap *
spn_extent_ring_snap_temp_alloc(struct spn_allocator_host_temp * const host_temp,
                                struct spn_device              * const device,
                                spn_result                    (* const wait)(struct spn_device * const device),
                                struct spn_extent_ring         * const ring)
{
  spn_subbuf_id_t id;

  struct spn_extent_ring_snap * snap =
    spn_allocator_host_temp_alloc(host_temp,
                                  device,
                                  wait,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*snap),
                                  &id,
                                  NULL);
  // save the id
  snap->id      = id;

  // back point to parent
  snap->ring    = ring;
  snap->next    = NULL;

  // save the inner boundaries of the ring to the snapshot
  snap->reads   = ring->inner.reads;
  snap->writes  = ring->inner.reads = ring->inner.writes;

  // mark not free
  snap->is_free = false;

  // attach snap to ring
  if (ring->head == NULL)
    {
      ring->head = snap;
      ring->last = snap;
    }
  else
    {
      ring->last->next = snap;
      ring->last       = snap;
    }

  return snap;
}

//
//
//

void
spn_extent_ring_snap_temp_free(struct spn_allocator_host_temp * const host_temp,
                               struct spn_extent_ring_snap    * const snap)
{
  // snap will be lazily freed
  snap->is_free = true;

  //
  // if this snapshot is no longer referenced then try to dispose of
  // the ring buffer's leading unreferenced snapshots
  //
  struct spn_extent_ring      * const ring = snap->ring;
  struct spn_extent_ring_snap *       curr = ring->head;

  if (!curr->is_free)
    return;

  do {
    // increment read counter
    ring->outer.reads = curr->writes;

    struct spn_extent_ring_snap * const next = curr->next;

    spn_allocator_host_temp_free(host_temp,curr->id);

    curr = next;

    // this was the last snap...
    if (curr == NULL)
      {
        ring->last = NULL;
        break;
      }

    // is the next free?
  } while (curr->is_free);

  // update head
  ring->head = curr;
}

//
//
//

uint32_t
spn_extent_ring_snap_count(struct spn_extent_ring_snap const * const snap)
{
  return snap->writes - snap->reads;
}

uint32_t
spn_extent_ring_snap_from(struct spn_extent_ring_snap const * const snap)
{
  return snap->reads & snap->ring->size.mask;
}

uint32_t
spn_extent_ring_snap_to(struct spn_extent_ring_snap const * const snap)
{
  return snap->writes & snap->ring->size.mask;
}

//
//
//
