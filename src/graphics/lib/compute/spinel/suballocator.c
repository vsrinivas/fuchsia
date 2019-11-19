// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "suballocator.h"

#include <assert.h>
#include <memory.h>

#include "allocator_host.h"
#include "common/macros.h"
#include "spinel/spinel_assert.h"
#include "trace.h"

//
// FIXME(allanmac): remove these once tracing is reliable
//

#if 0 && !defined(NDEBUG)

#include <stdio.h>

#define SPN_SUBALLOCATOR_DEBUG_ALLOC(suballocator, subbuf_id, ss)                                  \
  fprintf(stderr,                                                                                  \
          "suballocator %s : [ %4u ] : alloc( %9u ) @ %4u = %u\n",                                 \
          suballocator->name,                                                                      \
          suballocator->rem.avail,                                                                 \
          (uint32_t)ss,                                                                            \
          subbuf_id,                                                                               \
          (uint32_t)suballocator->total);

#define SPN_SUBALLOCATOR_DEBUG_FREE(suballocator, subbuf_id, ss)                                   \
  fprintf(stderr,                                                                                  \
          "suballocator %s : [ %4u ] : free ( %9u ) @ %4u = %u\n",                                 \
          suballocator->name,                                                                      \
          suballocator->rem.avail,                                                                 \
          (uint32_t)ss,                                                                            \
          subbuf_id,                                                                               \
          (uint32_t)suballocator->total);

#else

#define SPN_SUBALLOCATOR_DEBUG_ALLOC(suballocator, subbuf_id, ss)
#define SPN_SUBALLOCATOR_DEBUG_FREE(suballocator, subbuf_id, ss)

#endif

//
//
//

struct spn_subbuf
{
  struct spn_subbuf * prev;
  struct spn_subbuf * next;

  spn_subbuf_size_t size;
  spn_subbuf_size_t origin;

  uint32_t idx;  // ids[] index of subbuf in available state
  uint32_t inuse;
};

//
//
//

void
spn_suballocator_create(struct spn_suballocator * const        suballocator,
                        struct spn_allocator_host_perm * const host_perm,
                        char const * const                     name,
                        uint32_t const                         subbufs,
                        uint64_t const                         size,
                        uint64_t const                         alignment)
{
  suballocator->size  = (spn_subbuf_size_t)size;
  suballocator->total = 0;

  suballocator->rem.avail = 1;
  suballocator->rem.spare = subbufs - 1;

  suballocator->alignment = (uint32_t)alignment;
  suballocator->count     = subbufs;

  SPN_TRACE_SUBALLOCATOR_CREATE(name, suballocator, subbufs, size);

  //
  // allocate array of subbuf records
  //
  size_t const subbufs_size = sizeof(*suballocator->subbufs) * subbufs;

  suballocator->subbufs =
    spn_allocator_host_perm_alloc(host_perm, SPN_MEM_FLAGS_READ_WRITE, subbufs_size);

  // zero subbufs
  memset(suballocator->subbufs, 0, subbufs_size);

  //
  // initialize starting subbuf
  //
  suballocator->subbufs[0].size = (spn_subbuf_size_t)size;

  // allocate array of ids
  suballocator->ids = spn_allocator_host_perm_alloc(host_perm,
                                                    SPN_MEM_FLAGS_READ_WRITE,
                                                    sizeof(*suballocator->ids) * subbufs);
  for (uint32_t ii = 0; ii < subbufs; ii++)
    suballocator->ids[ii] = ii;

#ifndef NDEBUG
  suballocator->name = name;
#endif
}

void
spn_suballocator_dispose(struct spn_suballocator * const        suballocator,
                         struct spn_allocator_host_perm * const host_perm)
{
  spn_allocator_host_perm_free(host_perm, suballocator->ids);
  spn_allocator_host_perm_free(host_perm, suballocator->subbufs);
}

//
// Sets id and returns origin
//

void
spn_suballocator_subbuf_alloc(struct spn_suballocator * const suballocator,
                              struct spn_device * const       device,
                              spn_result_t (*const wait)(struct spn_device * const device),
                              uint64_t const          size,
                              spn_subbuf_id_t * const subbuf_id,
                              uint64_t * const        subbuf_origin,
                              uint64_t * const        subbuf_size)
{
  //
  // Note that we can't deadlock here because everything allocated is
  // expected to be freed within msecs.  Worst case, we wait for a
  // availability of resources while a fully utilized GPU is making
  // forward progress on kernels.
  //
  // This behavior should guide the sizing of the suballocator's
  // number of subbuffers and extent.
  //
  // We want to allocate a large enough extent and enough subbuffer
  // records so that the CPU/GPU is never starved.
  //

  // round up the size
  spn_subbuf_size_t const size_ru =
    (spn_subbuf_size_t)ROUND_UP_POW2_MACRO(size, suballocator->alignment);

  // save it
  if (subbuf_size != NULL)
    *subbuf_size = size_ru;

  //
  // We precheck to see there is at least one region of memory
  // available but do not check to see if there is a spare. Instead,
  // we simply keep looking for an exact fit.
  //
  spn_subbuf_id_t * const ids = suballocator->ids;

  while (true)
    {
      uint32_t avail_rem = suballocator->rem.avail;
      uint32_t spare_rem = suballocator->rem.spare;

      for (uint32_t avail_idx = 0; avail_idx < avail_rem; avail_idx++)
        {
          spn_subbuf_id_t const     avail_id = ids[avail_idx];
          struct spn_subbuf * const avail    = suballocator->subbufs + avail_id;

          assert(avail->inuse == 0);

          if (avail->size == size_ru)  // size matches exactly
            {
              suballocator->total += size_ru;

              SPN_SUBALLOCATOR_DEBUG_ALLOC(suballocator, avail_id, size_ru);

              // mark the subbuffer as in use
              avail->inuse += 1;

              assert(avail->inuse == 1);

              // update rem avail count
              suballocator->rem.avail = --avail_rem;

              // replace now inuse id with last avail id
              if ((avail_rem > 0) && (avail_idx != avail_rem))
                {
                  spn_subbuf_id_t const     last_id = ids[avail_rem];
                  struct spn_subbuf * const last    = suballocator->subbufs + last_id;

                  ids[avail_idx] = last_id;    // move id
                  last->idx      = avail_idx;  // update idx[]
                }

              assert(suballocator->rem.avail > 0);

              // return id and origin
              *subbuf_id     = avail_id;
              *subbuf_origin = avail->origin;

              return;
            }
          else if ((avail->size > size_ru) &&
                   (spare_rem > 0))  // requested is less than available so split it
            {
              suballocator->total += size_ru;

              uint32_t                  spare_idx = suballocator->count - spare_rem;
              spn_subbuf_id_t const     spare_id  = ids[spare_idx];
              struct spn_subbuf * const spare     = suballocator->subbufs + spare_id;

              assert(spare->inuse == 0);

              // simple -- we're popping the top-of-stack of spares
              suballocator->rem.spare -= 1;

              SPN_SUBALLOCATOR_DEBUG_ALLOC(suballocator, spare_id, size_ru);

              // get prev
              struct spn_subbuf * const prev = avail->prev;

              if (prev != NULL)
                prev->next = spare;

              // init spare
              spare->prev   = prev;
              spare->next   = avail;
              spare->size   = size_ru;
              spare->origin = avail->origin;
              spare->idx    = UINT32_MAX;  // defensive
              spare->inuse += 1;

              // update curr
              // clang-format off
              avail->prev    = spare;
              avail->size   -= size_ru;
              avail->origin += size_ru;
              // clang-format on

              assert(suballocator->rem.avail > 0);

              SPN_TRACE_SUBALLOCATOR_ALLOC(suballocator, spare_id, size_ru);

              // return id & origin
              *subbuf_id     = spare_id;
              *subbuf_origin = spare->origin;

              return;
            }
        }

      // uh oh... couldn't find enough memory
      spn_ok(wait(device));
    }
}

//
// FIXME -- simplify this with a merge-with-prev() primitive
//

void
spn_suballocator_subbuf_free(struct spn_suballocator * const suballocator,
                             spn_subbuf_id_t                 subbuf_id)
{
  if (subbuf_id == SPN_SUBBUF_ID_INVALID)
    return;

  // get subbuf for id
  struct spn_subbuf * const subbuf = suballocator->subbufs + subbuf_id;

  assert(subbuf->inuse == 1);

  suballocator->total -= subbuf->size;

  SPN_SUBALLOCATOR_DEBUG_FREE(suballocator, subbuf_id, subbuf->size);

  SPN_TRACE_SUBALLOCATOR_FREE(suballocator, subbuf_id, subbuf->size);

  //
  // try to merge subbuf with left and maybe right and then dispose
  //
  struct spn_subbuf * prev;
  struct spn_subbuf * next;

  if (((prev = subbuf->prev) != NULL) && !prev->inuse)
    {
      next = subbuf->next;

      if ((next != NULL) && !next->inuse)
        {
          subbuf->inuse -= 1;

          assert(next->inuse == 0);

          // increment size
          prev->size += (subbuf->size + next->size);

          struct spn_subbuf * const nextnext = next->next;

          // update next link
          prev->next = nextnext;

          // update prev link
          if (nextnext != NULL)
            nextnext->prev = prev;

          //
          // both subbuf and next are now spare which means we need to
          // move final available subbuffer into next's old position
          // unless they're the same
          //
          uint32_t const last_idx = --suballocator->rem.avail;
          uint32_t const next_idx = next->idx;

          assert(suballocator->rem.avail > 0);

          if (last_idx != next_idx)
            {
              spn_subbuf_id_t const     last_id = suballocator->ids[last_idx];
              struct spn_subbuf * const last    = suballocator->subbufs + last_id;

              suballocator->ids[next_idx] = last_id;
              last->idx                   = next_idx;
            }

          spn_subbuf_id_t const next_id = (spn_subbuf_id_t)(next - suballocator->subbufs);

          uint32_t const spare_rem = suballocator->rem.spare + 2;
          uint32_t const spare_idx = suballocator->count - spare_rem;

          suballocator->rem.spare          = spare_rem;
          suballocator->ids[spare_idx + 0] = subbuf_id;
          suballocator->ids[spare_idx + 1] = next_id;
        }
      else
        {
          prev->size += subbuf->size;
          prev->next = next;

          if (next != NULL)
            next->prev = prev;

          subbuf->inuse -= 1;

          assert(subbuf->inuse == 0);
          assert(suballocator->rem.avail > 0);

          suballocator->ids[suballocator->count - ++suballocator->rem.spare] = subbuf_id;
        }
    }
  //
  // try to merge with right
  //
  else if (((next = subbuf->next) != NULL) && !next->inuse)
    {
      subbuf->inuse -= 1;

      assert(subbuf->inuse == 0);
      assert(suballocator->rem.avail > 0);

      next->prev   = prev;
      next->origin = subbuf->origin;
      next->size += subbuf->size;

      if (prev != NULL)
        prev->next = next;

      // subbuf is now spare
      suballocator->ids[suballocator->count - ++suballocator->rem.spare] = subbuf_id;
    }
  else  // couldn't merge with a neighbor
    {
      uint32_t avail_idx = suballocator->rem.avail++;

      // subbuf is now available
      subbuf->idx = avail_idx;
      subbuf->inuse -= 1;

      assert(subbuf->inuse == 0);
      assert(suballocator->rem.avail > 0);

      suballocator->ids[avail_idx] = subbuf_id;
    }
}

//
//
//

#if 0

//
// At some point there might be a reason to sort the available
// subbuffers into some useful order -- presumably to binary search
// for the closest match or to chip away at the largest available
// subbuffer
//

static
void
spn_suballocator_optimize(struct spn_suballocator * const suballocator)
{
  ;
}

#endif

//
//
//
