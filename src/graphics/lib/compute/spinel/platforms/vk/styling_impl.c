// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "styling_impl.h"

#include "cb_pool.h"
#include "common/vk/vk_assert.h"
#include "device.h"
#include "queue_pool.h"
#include "semaphore_pool.h"
#include "state_assert.h"
#include "target.h"

//
// Styling states
//

typedef enum spn_si_state_e
{

  SPN_SI_STATE_UNSEALED,
  SPN_SI_STATE_SEALING,
  SPN_SI_STATE_SEALED

} spn_si_state_e;

//
//
//

struct spn_si_vk
{
  struct
  {
    VkDescriptorBufferInfo dbi;
    VkDeviceMemory         dm;
  } h;

  struct
  {
    VkDescriptorBufferInfo dbi;
    VkDeviceMemory         dm;
  } d;

  struct
  {
    VkSemaphore sealing;
  } semaphore;
};

//
// IMPL
//

struct spn_styling_impl
{
  struct spn_styling *             styling;
  struct spn_device *              device;
  struct spn_target_config const * config;
  struct spn_si_vk                 vk;

  SPN_ASSERT_STATE_DECLARE(spn_si_state_e);

  uint32_t lock_count;  // # of wip renders
};

//
// A callback is only invoked if a H2D copy is required.
//

struct spn_si_complete_payload
{
  struct spn_styling_impl * impl;
  VkCommandBuffer           cb;
};

static void
spn_si_complete(void * pfn_payload)
{
  //
  // FENCE_POOL INVARIANT:
  //
  // COMPLETION ROUTINE MUST MAKE LOCAL COPIES OF PAYLOAD BEFORE ANY
  // POTENTIAL INVOCATION OF SPN_DEVICE_YIELD/WAIT/DRAIN()
  //
  struct spn_si_complete_payload const * const payload = pfn_payload;
  struct spn_styling_impl * const              impl    = payload->impl;

  // release the copy semaphore
  spn_device_semaphore_pool_release(impl->device, impl->vk.semaphore.sealing);

  // and we're sealed...
  impl->state = SPN_SI_STATE_SEALED;
}

//
//
//

static spn_result
spn_si_seal(struct spn_styling_impl * const impl)
{
  //
  // return if SEALING or SEALED
  //
  if (impl->state >= SPN_SI_STATE_SEALING)
    return SPN_SUCCESS;

  struct spn_device * const device = impl->device;

  //
  // otherwise, kick off the UNSEALED > SEALING > SEALED transition
  //
  if (impl->config->styling.vk.d != 0)
    {
      //
      // We need to copy styling data from the host to device
      //

      // move to SEALING state
      impl->state = SPN_SI_STATE_SEALING;

      // acquire the semaphore associated with the copy
      impl->vk.semaphore.sealing = spn_device_semaphore_pool_acquire(device);

      // get a cb
      VkCommandBuffer cb = spn_device_cb_acquire_begin(device);

      //
      // launch a copy and record a semaphore
      //
      VkBufferCopy const bc = {.srcOffset = impl->vk.h.dbi.offset,
                               .dstOffset = impl->vk.d.dbi.offset,
                               .size      = impl->styling->dwords.next * sizeof(uint32_t)};

      vkCmdCopyBuffer(cb, impl->vk.h.dbi.buffer, impl->vk.d.dbi.buffer, 1, &bc);

      struct spn_si_complete_payload payload = {.impl = impl, .cb = cb};

      VkFence const fence =
        spn_device_cb_end_fence_acquire(device, cb, spn_si_complete, &payload, sizeof(payload));
      // boilerplate submit
      struct VkSubmitInfo const si = {.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                      .pNext                = NULL,
                                      .waitSemaphoreCount   = 0,
                                      .pWaitSemaphores      = NULL,
                                      .pWaitDstStageMask    = NULL,
                                      .commandBufferCount   = 1,
                                      .pCommandBuffers      = &cb,
                                      .signalSemaphoreCount = 1,
                                      .pSignalSemaphores    = &impl->vk.semaphore.sealing};

      vk(QueueSubmit(spn_device_queue_next(device), 1, &si, fence));
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

static spn_result
spn_si_unseal(struct spn_styling_impl * const impl)
{
  //
  // return if already unsealed
  //
  if (impl->state == SPN_SI_STATE_UNSEALED)
    return SPN_SUCCESS;

  struct spn_device * const device = impl->device;

  //
  // otherwise, we know we're either SEALING or SEALED
  //
  while (impl->state != SPN_SI_STATE_SEALED)
    {
      // wait for SEALING > SEALED transition ...
      spn_device_wait(device);
    }

  //
  // wait for any rendering locks to be released
  //
  while (impl->lock_count > 0)
    {
      spn_device_wait(device);
    }

  //
  // and we're done
  //
  impl->state = SPN_SI_STATE_UNSEALED;

  return SPN_SUCCESS;
}

//
//
//

static spn_result
spn_si_release(struct spn_styling_impl * const impl)
{
  //
  // was this the last reference?
  //
  if (--impl->styling->ref_count != 0)
    return SPN_SUCCESS;

  struct spn_device * const device = impl->device;

  //
  // wait for any in-flight renders to complete
  //
  while (impl->lock_count > 0)
    {
      spn_device_wait(device);
    }

  //
  // Note that we don't have to unmap before freeing
  //

  //
  // free device allocations
  //
  if (impl->config->styling.vk.d != 0)
    {
      spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                     device->vk,
                                     &impl->vk.d.dbi,
                                     impl->vk.d.dm);
    }

  spn_allocator_device_perm_free(&device->allocator.device.perm.coherent,
                                 device->vk,
                                 &impl->vk.h.dbi,
                                 impl->vk.h.dm);

  //
  // free host allocations
  //
  struct spn_allocator_host_perm * const perm = &impl->device->allocator.host.perm;

  spn_allocator_host_perm_free(perm, impl->styling);
  spn_allocator_host_perm_free(perm, impl);

  return SPN_SUCCESS;
}

//
//
//

#ifdef SPN_DISABLED_UNTIL_INTEGRATED

static void
spn_si_retain_and_lock(struct spn_styling_impl * const impl)
{
  impl->styling->ref_count += 1;

  impl->lock_count += 1;
}

static void
spn_styling_unlock_and_release(struct spn_styling_impl * const impl)
{
  impl->lock_count -= 1;

  spn_si_release(impl);
}

#endif

//
//
//

spn_result
spn_styling_impl_create(struct spn_device * const   device,
                        struct spn_styling ** const styling,
                        uint32_t const              dwords_count,
                        uint32_t const              layers_count)
{
  //
  // retain the context
  // spn_context_retain(context);
  //
  struct spn_allocator_host_perm * const perm = &device->allocator.host.perm;

  //
  // allocate impl
  //
  struct spn_styling_impl * const impl =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*impl));
  //
  // allocate styling
  //
  struct spn_styling * const s =
    spn_allocator_host_perm_alloc(perm, SPN_MEM_FLAGS_READ_WRITE, sizeof(*s));

  // init impl and pb back-pointers
  *styling      = s;
  impl->styling = s;
  s->impl       = impl;

  // save device
  impl->device = device;

  struct spn_target_config const * const config = spn_target_get_config(device->target);

  impl->config = config;

  impl->lock_count = 0;

  //
  // initialize styling
  //
  s->seal    = spn_si_seal;
  s->unseal  = spn_si_unseal;
  s->release = spn_si_release;

  s->dwords.count = dwords_count;
  s->dwords.next  = layers_count * SPN_STYLING_LAYER_COUNT_DWORDS;

  s->layers.count = layers_count;

  s->ref_count = 1;

  //
  // initialize styling extent
  //
  size_t const styling_size = dwords_count * sizeof(uint32_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.coherent,
                                  device->vk,
                                  dwords_count * sizeof(uint32_t),
                                  NULL,
                                  &impl->vk.h.dbi,
                                  &impl->vk.h.dm);

  vk(MapMemory(device->vk->d, impl->vk.h.dm, 0, VK_WHOLE_SIZE, 0, (void **)&s->extent));

  if (config->styling.vk.d != 0)
    {
      spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                      device->vk,
                                      styling_size,
                                      NULL,
                                      &impl->vk.d.dbi,
                                      &impl->vk.d.dm);
    }
  else
    {
      impl->vk.d.dbi = (VkDescriptorBufferInfo){.buffer = VK_NULL_HANDLE, .offset = 0, .range = 0};
      impl->vk.d.dm  = VK_NULL_HANDLE;
    }

  // the styling impl starts out unsealed
  SPN_ASSERT_STATE_INIT(impl, SPN_SI_STATE_UNSEALED);

  return SPN_SUCCESS;
}

//
//
//

void
spn_styling_impl_pre_render_ds(struct spn_styling * const             styling,
                               struct spn_target_ds_styling_t * const ds,
                               VkCommandBuffer                        cb)
{
  struct spn_styling_impl * const impl   = styling->impl;
  struct spn_device * const       device = impl->device;
  struct spn_target * const       target = device->target;

  assert(impl->state >= SPN_SI_STATE_SEALING);

  //
  // acquire STYLING descriptor set
  //

  spn_target_ds_acquire_styling(target, device, ds);

  // copy the dbi structs
  *spn_target_ds_get_styling_styling(target, *ds) = impl->vk.d.dbi;

  // update ds
  spn_target_ds_update_styling(target, device->vk, *ds);

  // bind
  spn_target_ds_bind_render_styling(target, cb, *ds);
}

//
//
//

void
spn_styling_impl_pre_render_wait(struct spn_styling * const   styling,
                                 uint32_t * const             waitSemaphoreCount,
                                 VkSemaphore * const          pWaitSemaphores,
                                 VkPipelineStageFlags * const pWaitDstStageMask)
{
  struct spn_styling_impl * const impl = styling->impl;

  assert(impl->state >= SPN_SI_STATE_SEALING);

  if (impl->state == SPN_SI_STATE_SEALING)
    {
      uint32_t const idx = (*waitSemaphoreCount)++;

      pWaitSemaphores[idx]   = impl->vk.semaphore.sealing;
      pWaitDstStageMask[idx] = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
}

//
//
//
