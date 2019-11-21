// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status.h"

#include <stdio.h>

#include "block_pool.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "core.h"
#include "device.h"
#include "dispatch.h"
#include "spinel_assert.h"
#include "vk.h"
#include "vk_target.h"

//
//
//

struct spn_status
{
  struct spn_vk_ds_status_t ds_status;

  struct
  {
    VkDescriptorBufferInfo * dbi;
    VkDeviceMemory           dm;

    SPN_VK_BUFFER_NAME(status, status) * mapped;
  } h;
};

//
//
//

void
spn_device_status_create(struct spn_device * const device)
{
  struct spn_status * const status = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                                   SPN_MEM_FLAGS_READ_WRITE,
                                                                   sizeof(*status));

  device->status = status;

  struct spn_vk * const instance = device->instance;

  // get a descriptor set -- there is only one per Spinel device!
  spn_vk_ds_acquire_status(instance, device, &status->ds_status);

  // get descriptor set DBIs
  status->h.dbi = spn_vk_ds_get_status_status(instance, status->ds_status);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.copyback,
                                  &device->environment,
                                  sizeof(*status->h.mapped),
                                  NULL,
                                  status->h.dbi,
                                  &status->h.dm);

  vk(MapMemory(device->environment.d,
               status->h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&status->h.mapped));

  // update the status ds
  spn_vk_ds_update_status(instance, &device->environment, status->ds_status);
}

//
//
//

void
spn_device_status_dispose(struct spn_device * const device)
{
  struct spn_vk * const     instance = device->instance;
  struct spn_status * const status   = device->status;

  spn_vk_ds_release_status(instance, status->ds_status);

  spn_allocator_device_perm_free(&device->allocator.device.perm.copyback,
                                 &device->environment,
                                 status->h.dbi,
                                 status->h.dm);

  spn_allocator_host_perm_free(&device->allocator.host.perm, device->status);
}

//
//
//

spn_result_t
spn_device_get_status(struct spn_device * const device)
{
  //
  // drain all work in flight
  //
  spn(device_wait_all(device, true));

  //
  // prepare a dispatch
  //
  spn_dispatch_id_t id;

  spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_STATUS, &id));

  VkCommandBuffer cb = spn_device_dispatch_get_cb(device, id);

  //
  struct spn_vk * const     instance = device->instance;
  struct spn_status * const status   = device->status;

  // bind the global block pool
  spn_vk_ds_bind_get_status_block_pool(instance, cb, spn_device_block_pool_get_ds(device));

  // bind the status
  spn_vk_ds_bind_get_status_status(instance, cb, status->ds_status);

  // bind pipeline
  spn_vk_p_bind_get_status(instance, cb);

  // dispatch the pipeline
  vkCmdDispatch(cb, 1, 1, 1);

  // for debugging
#ifdef SPN_BP_DEBUG
  spn_device_block_pool_debug_snap(device, cb);
#endif

  // make the copyback visible to the host
  vk_barrier_compute_w_to_host_r(cb);

  // launch!
  spn_device_dispatch_submit(device, id);

  //
  // wait for completion
  //
  spn(device_wait_all(device, true));

  //
  // print out the results
  //
  // FIXME(allanmac): we can return status info a struct at some point
  // instead of a noisy print.
  //
  {
    struct spn_vk_target_config const * const config = spn_vk_get_config(instance);

    size_t const block_bytes = sizeof(uint32_t) << config->block_pool.block_dwords_log2;

    uint32_t const reads   = status->h.mapped->status_bp_atomics[SPN_BLOCK_POOL_ATOMICS_READS];
    uint32_t const writes  = status->h.mapped->status_bp_atomics[SPN_BLOCK_POOL_ATOMICS_WRITES];
    uint32_t const avail   = writes - reads;
    uint32_t const bp_size = spn_device_block_pool_get_size(device);
    uint32_t const inuse   = bp_size - avail;

    fprintf(stderr,
            "writes/reads/avail/alloc: %9u / %9u / %9u = %9.3f MB / %9u = %9.3f MB\n",
            writes,
            reads,
            avail,
            (block_bytes * avail) / (1024.0 * 1024.0),
            inuse,
            (block_bytes * inuse) / (1024.0 * 1024.0));
  }

  //
  // Temporarily dump the debug buffer here
  //
#ifdef SPN_BP_DEBUG
  spn_device_block_pool_debug_print(device);
#endif

  return SPN_SUCCESS;
}

//
//
//
