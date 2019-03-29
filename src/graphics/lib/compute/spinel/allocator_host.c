// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdlib.h>
#include <assert.h>

//
//
//

#include "allocator_host.h"

//
//
//

#if defined( _MSC_VER )

#define SPN_ALIGNED_ALLOC(alignment,size)  _aligned_malloc(size,alignment)
#define SPN_ALIGNED_FREE(p)                _aligned_free(p)

#else

#define SPN_ALIGNED_ALLOC(alignment,size)  aligned_alloc(alignment,size)
#define SPN_ALIGNED_FREE(p)                free(p)

#endif

//
// PERM
//

#ifndef NDEBUG

#include <stdbool.h>

bool
is_pow2_u64(uint64_t n)
{
  return (n & (n-1)) == 0;
}

#endif

void
spn_allocator_host_perm_create(struct spn_allocator_host_perm * const host_perm,
                               uint64_t                         const alignment)
{
  assert(is_pow2_u64(alignment));

  host_perm->alignment = alignment;
}

void
spn_allocator_host_perm_dispose(struct spn_allocator_host_perm * const host_perm)
{
  ;
}

void *
spn_allocator_host_perm_alloc(struct spn_allocator_host_perm * const host_perm,
                              spn_mem_flags_e                  const flags,
                              uint64_t                         const size)
{
  uint64_t const mask    = host_perm->alignment - 1;
  uint64_t const size_ru = (size + mask) & ~mask;

  return SPN_ALIGNED_ALLOC(host_perm->alignment,size_ru);
}

void
spn_allocator_host_perm_free(struct spn_allocator_host_perm * const host_perm,
                             void                           * const mem)
{
  SPN_ALIGNED_FREE(mem);
}

//
// TEMP
//

void
spn_allocator_host_temp_create(struct spn_allocator_host_temp * const host_temp,
                               struct spn_allocator_host_perm * const host_perm,
                               uint32_t                         const subbufs,
                               uint64_t                         const size,
                               uint64_t                         const alignment)
{
  // round it up
  uint64_t const mask    = alignment - 1;
  uint64_t const size_ru = (size + mask) & ~mask;

  host_temp->host_perm   = host_perm;

  host_temp->extent      = spn_allocator_host_perm_alloc(host_perm,
                                                         SPN_MEM_FLAGS_READ_WRITE,
                                                         size_ru);
  spn_suballocator_create(&host_temp->suballocator,
                          host_perm,
                          "HOST ",
                          subbufs,
                          size_ru,
                          alignment);
}

void
spn_allocator_host_temp_dispose(struct spn_allocator_host_temp * const host_temp)
{
  spn_suballocator_dispose(&host_temp->suballocator,host_temp->host_perm);

  spn_allocator_host_perm_free(host_temp->host_perm,host_temp->extent);
}


void *
spn_allocator_host_temp_alloc(struct spn_allocator_host_temp * const host_temp,
                              struct spn_device              * const device,
                              spn_result                    (* const wait)(struct spn_device * const device),
                              spn_mem_flags_e                  const flags,
                              uint64_t                         const size,
                              spn_subbuf_id_t                * const subbuf_id,
                              uint64_t                       * const subbuf_size)
{
  if (size == 0)
    {
      *subbuf_id = (spn_subbuf_id_t)-1;

      if (subbuf_size != NULL)
        *subbuf_size = 0;

      return NULL;
    }

  uint64_t subbuf_origin;

  spn_suballocator_subbuf_alloc(&host_temp->suballocator,
                                device,
                                wait,
                                size,
                                subbuf_id,
                                &subbuf_origin,
                                subbuf_size);

  return host_temp->extent + (spn_subbuf_size_t)subbuf_origin;
}


void
spn_allocator_host_temp_free(struct spn_allocator_host_temp * const host_temp,
                             spn_subbuf_id_t                  const subbuf_id)
{
  spn_suballocator_subbuf_free(&host_temp->suballocator,subbuf_id);
}

//
//
//
