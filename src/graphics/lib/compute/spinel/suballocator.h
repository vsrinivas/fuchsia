// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SUBALLOCATOR_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SUBALLOCATOR_H_

//
// NOTE(allanmac): We may want to move this into 'common'...
//
// NOTE(allanmac): It's more likely we get rid of this and replace it
// with a ring/bump allocator because the allocation patterns are
// simpler than in the previous CUDA and OpenCL implementations.
//

//
// This is a suballocator for a large extent typically less than 4GB.
//
// The SPN pipeline will use this for ephemeral host and device memory
// allocations.  The lifetime of an allocation is typically
// milliseconds or less and is associated with either a single kernel
// or a sub-pipeline.
//
// Because of this, a relatively small number of allocations (10's)
// will be outstanding at any time so the implementation can
// reasonably be very simplistic and optimize for this case.
//
// The contract between Spinel and the ephermeral suballocations is
// that if either memory or subbuffer nodes aren't available the
// suballocator will block and pump the context scheduler until it can
// proceed.
//
// Note that this implementation is single-threaded and the
// suballocator's state may have been altered after pumping the
// scheduler.
//

#include <stdint.h>

#include "spinel/spinel_result.h"

//
//
//

struct spn_allocator_host_perm;
struct spn_device;

//
// It's practical for the subbuf_id to be either 16 bits or maybe even
// 8 bits if the number of outstanding subbufs is in the thousands (16
// bits) or under 256 (8 bits).
//

typedef uint16_t spn_subbuf_id_t;

#define SPN_SUBBUF_ID_INVALID ((spn_subbuf_id_t)-1)

#ifndef SPN_SUBALLOCATOR_GTE_4GB
typedef uint32_t spn_subbuf_size_t;  // < 4GB
#else
typedef uint64_t spn_subbuf_size_t;  // >=4GB
#endif

//
// This structure is purposefully public -- there is no need to hide
// it and it simplifies context allocation.
//

//
// clang-format off
//

struct spn_suballocator
{
  struct
  {
    uint32_t          avail;
    uint32_t          spare;
  } rem;                          // inuse = count - (avail + spare)

  spn_subbuf_size_t   size;       // size of memory extent
  spn_subbuf_size_t   total;      // total outstanding allocations

  uint32_t            alignment;  // required pow2 alignment
  uint32_t            count;      // number of subbufs

  struct spn_subbuf * subbufs;

  spn_subbuf_id_t   * ids;        // [<-AVAIL-><-empty-><-SPARE->]

#ifndef NDEBUG
  char const        * name;
#endif
};

//
// clang-format on
//

//
// Assumes 'size' is a multiple of power-of-two 'align'
//

void
spn_suballocator_create(struct spn_suballocator * const        suballocator,
                        struct spn_allocator_host_perm * const host_perm,
                        char const * const                     name,
                        uint32_t const                         subbufs,
                        uint64_t const                         size,
                        uint64_t const                         alignment);

void
spn_suballocator_dispose(struct spn_suballocator * const        suballocator,
                         struct spn_allocator_host_perm * const host_perm);

//
// FIXME(allanmac): go ahead and create a typedef for the wait function
// so that clang-format stops mangling the prototype.
//

void
spn_suballocator_subbuf_alloc(struct spn_suballocator * const suballocator,
                              struct spn_device * const       device,
                              spn_result_t (*const wait)(struct spn_device * const device),
                              uint64_t const          size,
                              spn_subbuf_id_t * const subbuf_id,
                              uint64_t * const        subbuf_origin,
                              uint64_t * const        subbuf_size);

void
spn_suballocator_subbuf_free(struct spn_suballocator * const suballocator,
                             spn_subbuf_id_t                 subbuf_id);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SUBALLOCATOR_H_
