// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <stdint.h>

#include "suballocator.h"

//
//
//

typedef enum spn_mem_flags_e {

  SPN_MEM_FLAGS_READ_WRITE,
  // SPN_MEM_FLAGS_WRITE_ONLY,
  // SPN_MEM_FLAGS_READ_ONLY

} spn_mem_flags_e;

//
// Permanent / durable allocations
//

struct spn_allocator_host_perm
{
  uint64_t alignment;
};

//
// Temporary / ephemeral allocations
//
// Note that the temp allocator references the perm allocator used for
// creation because there might be more than one.
//

struct spn_allocator_host_temp
{
  struct spn_allocator_host_perm * host_perm;
  uint8_t                        * extent;
  struct spn_suballocator          suballocator;
};

//
// PERM
//

void
spn_allocator_host_perm_create(struct spn_allocator_host_perm * const host_perm,
                               uint64_t                         const alignment);

void
spn_allocator_host_perm_dispose(struct spn_allocator_host_perm * const host_perm);


void *
spn_allocator_host_perm_alloc(struct spn_allocator_host_perm * const host_perm,
                              spn_mem_flags_e                  const flags,
                              uint64_t                         const size);

void
spn_allocator_host_perm_free(struct spn_allocator_host_perm * const host_perm,
                             void                           * const mem);
//
// TEMP
//

void
spn_allocator_host_temp_create(struct spn_allocator_host_temp * const host_temp,
                               struct spn_allocator_host_perm * const host_perm,
                               uint32_t                         const subbufs,
                               uint64_t                         const size,
                               uint64_t                         const alignment);

void
spn_allocator_host_temp_dispose(struct spn_allocator_host_temp * const host_temp);

void *
spn_allocator_host_temp_alloc(struct spn_allocator_host_temp * const host_temp,
                              struct spn_device              * const device,
                              spn_result                    (* const wait)(struct spn_device * const device),
                              spn_mem_flags_e                  const flags,
                              uint64_t                         const size,
                              spn_subbuf_id_t                * const subbuf_id,
                              uint64_t                       * const subbuf_size);

void
spn_allocator_host_temp_free(struct spn_allocator_host_temp * const host_temp,
                             spn_subbuf_id_t                  const subbuf_id);

//
//
//
