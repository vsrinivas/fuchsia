// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "styling_impl.h"

#include <stdlib.h>

#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "core.h"
#include "device.h"
#include "queue_pool.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_opcodes.h"
#include "state_assert.h"

//
// Styling states
//
typedef enum spinel_si_state_e
{
  SPN_SI_STATE_UNSEALED,
  SPN_SI_STATE_SEALING,
  SPN_SI_STATE_SEALED

} spinel_si_state_e;

//
// VK
//
struct spinel_si_vk
{
  struct spinel_dbi_dm_devaddr h;
  struct spinel_dbi_dm_devaddr d;
};

//
// IMPL
//
struct spinel_styling_impl
{
  struct spinel_styling * styling;
  struct spinel_device *  device;

  //
  // Vulkan resources
  //
  struct spinel_si_vk vk;

  uint32_t          lock_count;  // # of wip renders
  spinel_si_state_e state;

  struct
  {
    struct
    {
      spinel_deps_immediate_semaphore_t immediate;
    } sealing;
  } signal;
};

//
// A callback is only invoked if a H2D copy is required.
//
struct spinel_si_complete_payload
{
  struct spinel_styling_impl * impl;
};

//
//
//
static void
spinel_si_seal_complete(void * data0, void * data1)
{
  struct spinel_styling_impl * const impl = data0;

  impl->state                    = SPN_SI_STATE_SEALED;
  impl->signal.sealing.immediate = SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID;
}

//
// Record commands
//
static VkPipelineStageFlags
spinel_si_seal_record(VkCommandBuffer cb, void * data0, void * data1)
{
  struct spinel_styling_impl * const impl = data0;

  VkBufferCopy const bc = {

    .srcOffset = impl->vk.h.dbi_dm.dbi.offset,
    .dstOffset = impl->vk.d.dbi_dm.dbi.offset,
    .size      = impl->styling->dwords.next * sizeof(uint32_t)
  };

  vkCmdCopyBuffer(cb,  //
                  impl->vk.h.dbi_dm.dbi.buffer,
                  impl->vk.d.dbi_dm.dbi.buffer,
                  1,
                  &bc);

  //
  // This command buffer ends with a transfer
  //
  return VK_PIPELINE_STAGE_TRANSFER_BIT;
}

//
//
//
static spinel_result_t
spinel_si_seal(struct spinel_styling_impl * const impl)
{
  //
  // return if SEALING or SEALED
  //
  if (impl->state >= SPN_SI_STATE_SEALING)
    {
      return SPN_SUCCESS;
    }

  //
  // otherwise, kick off the UNSEALED > SEALING > SEALED transition
  //
  struct spinel_device * const device = impl->device;

  //
  // If this is a discrete GPU then styling data is copied from the host to
  // device.
  //
  struct spinel_target_config const * config = &device->ti.config;

  if ((config->allocator.device.hw_dr.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0)
    {
      //
      // Move to SEALING state
      //
      impl->state = SPN_SI_STATE_SEALING;

      //
      // Acquire an immediate semaphore
      //
      struct spinel_deps_immediate_submit_info const disi = {
        .record = {
          .pfn   = spinel_si_seal_record,
          .data0 = impl,
        },
        .completion = {
          .pfn   = spinel_si_seal_complete,
          .data0 = impl,
        },
      };

      struct spinel_device * const device = impl->device;

      spinel_deps_immediate_submit(device->deps,
                                   &device->vk,
                                   &disi,
                                   &impl->signal.sealing.immediate);
    }
  else
    {
      //
      // We don't need to copy from the host to device so just
      // transition directly to the SEALED state.
      //
      impl->state = SPN_SI_STATE_SEALED;
    }

  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_si_unseal(struct spinel_styling_impl * const impl)
{
  //
  // return if already unsealed
  //
  if (impl->state == SPN_SI_STATE_UNSEALED)
    {
      return SPN_SUCCESS;
    }

  //
  // otherwise, we know we're either SEALING or SEALED
  //
  struct spinel_device * const device = impl->device;

  while (impl->state != SPN_SI_STATE_SEALED)
    {
      // wait for SEALING > SEALED transition ...
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  //
  // wait for any rendering locks to be released
  //
  while (impl->lock_count > 0)
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  //
  // transition to unsealed
  //
  impl->state = SPN_SI_STATE_UNSEALED;

  return SPN_SUCCESS;
}

//
//
//
static spinel_result_t
spinel_si_release(struct spinel_styling_impl * const impl)
{
  //
  // wait for any in-flight renders to complete
  //
  struct spinel_device * const device = impl->device;

  while (impl->lock_count > 0)
    {
      spinel_deps_drain_1(device->deps, &device->vk);
    }

  //
  // free device allocations
  //
  struct spinel_target_config const * config = &device->ti.config;

  if ((config->allocator.device.hw_dr.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0)
    {
      //
      // Note that we don't have to unmap before freeing
      //
      spinel_allocator_free_dbi_dm(&device->allocator.device.perm.drw,
                                   device->vk.d,
                                   device->vk.ac,
                                   &impl->vk.d.dbi_dm);
    }

  spinel_allocator_free_dbi_dm(&device->allocator.device.perm.hw_dr,
                               device->vk.d,
                               device->vk.ac,
                               &impl->vk.h.dbi_dm);

  //
  // free host allocations
  //
  free(impl->styling);
  free(impl);

  spinel_context_release(device->context);

  return SPN_SUCCESS;
}

//
//
//
spinel_result_t
spinel_styling_impl_create(struct spinel_device *               device,
                           spinel_styling_create_info_t const * create_info,
                           spinel_styling_t *                   styling)
{
  spinel_context_retain(device->context);

  //
  // allocate impl
  //
  struct spinel_styling_impl * const impl = malloc(sizeof(*impl));

  //
  // allocate styling
  //
  struct spinel_styling * const s = *styling = malloc(sizeof(*s));

  //
  // init forward/backward pointers
  //
  impl->styling = s;
  impl->device  = device;
  s->impl       = impl;

  //
  // initialize styling pfns
  //
  s->seal         = spinel_si_seal;
  s->unseal       = spinel_si_unseal;
  s->release      = spinel_si_release;
  s->ref_count    = 1;
  s->layers.count = create_info->layer_count;

  uint32_t const layers_dwords = create_info->layer_count * SPN_STYLING_LAYER_COUNT_DWORDS;
  uint32_t const dwords_count  = layers_dwords + create_info->cmd_count;

  s->dwords.count = dwords_count;
  s->dwords.next  = layers_dwords;

  //
  // initialize rest of impl
  //
  impl->lock_count = 0;
  impl->state      = SPN_SI_STATE_UNSEALED;

  //
  //
  //
  impl->signal.sealing.immediate = SPN_DEPS_IMMEDIATE_SEMAPHORE_INVALID;

  //
  // initialize styling extent
  //
  VkDeviceSize const styling_size = sizeof(uint32_t) * dwords_count;

  spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.hw_dr,
                                        device->vk.pd,
                                        device->vk.d,
                                        device->vk.ac,
                                        styling_size,
                                        NULL,
                                        &impl->vk.h);

  vk(MapMemory(device->vk.d,  //
               impl->vk.h.dbi_dm.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&s->extent));

  struct spinel_target_config const * config = &device->ti.config;

  if ((config->allocator.device.hw_dr.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0)
    {
      spinel_allocator_alloc_dbi_dm_devaddr(&device->allocator.device.perm.drw,
                                            device->vk.pd,
                                            device->vk.d,
                                            device->vk.ac,
                                            styling_size,
                                            NULL,
                                            &impl->vk.d);
    }
  else
    {
      impl->vk.d = impl->vk.h;
    }

  return SPN_SUCCESS;
}

//
//
//
spinel_deps_immediate_semaphore_t
spinel_styling_retain_and_lock(struct spinel_styling * styling)
{
  struct spinel_styling_impl * const impl = styling->impl;

  assert(impl->state >= SPN_SI_STATE_SEALING);

  spinel_styling_retain(styling);

  impl->lock_count += 1;

  return impl->signal.sealing.immediate;
}

//
//
//
void
spinel_styling_unlock_and_release(struct spinel_styling * styling)
{
  styling->impl->lock_count -= 1;

  spinel_styling_release(styling);
}

//
// Initialize RENDER push constants with styling bufrefs
//
void
spinel_styling_push_render_init(struct spinel_styling *     styling,
                                struct spinel_push_render * push_render)
{
  push_render->devaddr_styling = styling->impl->vk.d.devaddr;
}

//
//
//
