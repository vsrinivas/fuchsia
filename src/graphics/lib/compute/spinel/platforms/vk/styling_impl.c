// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "styling_impl.h"

#include "common/vk/assert.h"
#include "device.h"
#include "queue_pool.h"
#include "spinel_assert.h"
#include "state_assert.h"
#include "vk.h"
#include "vk_target.h"

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
};

//
// IMPL
//

struct spn_styling_impl
{
  struct spn_styling *                styling;
  struct spn_device *                 device;
  struct spn_vk_target_config const * config;  // FIXME(allanmac): we don't need to duplicate this
  struct spn_si_vk                    vk;

  uint32_t lock_count;  // # of wip renders

  SPN_ASSERT_STATE_DECLARE(spn_si_state_e);  // FIXME(allanmac): clean up this state tracking

  spn_dispatch_id_t id;
};

//
// A callback is only invoked if a H2D copy is required.
//

struct spn_si_complete_payload
{
  struct spn_styling_impl * impl;
};

//
//
//

static void
spn_si_complete(void * pfn_payload)
{
  struct spn_si_complete_payload const * const payload = pfn_payload;
  struct spn_styling_impl * const              impl    = payload->impl;

  // and we're sealed...
  impl->state = SPN_SI_STATE_SEALED;
}

//
//
//

static spn_result_t
spn_si_seal(struct spn_styling_impl * const impl)
{
  //
  // return if SEALING or SEALED
  //
  if (impl->state >= SPN_SI_STATE_SEALING)
    return SPN_SUCCESS;

  //
  // otherwise, kick off the UNSEALED > SEALING > SEALED transition
  //
  struct spn_device * const device = impl->device;

  //
  // If we're on a discrete GPU then copy styling data from the host to
  // device.
  //
  if (impl->config->styling.vk.d != 0)
    {
      // move to SEALING state
      impl->state = SPN_SI_STATE_SEALING;

      // acquire a dispatch
      spn(device_dispatch_acquire(device, SPN_DISPATCH_STAGE_STYLING, &impl->id));

      // get a cb
      VkCommandBuffer cb = spn_device_dispatch_get_cb(device, impl->id);

      //
      // copy the styling buffer
      //
      // FIXME(allanmac): this can be made more sophisticated once
      // random-access styling operations are added.
      //
      VkBufferCopy const bc = { .srcOffset = impl->vk.h.dbi.offset,
                                .dstOffset = impl->vk.d.dbi.offset,
                                .size      = impl->styling->dwords.next * sizeof(uint32_t) };

      vkCmdCopyBuffer(cb, impl->vk.h.dbi.buffer, impl->vk.d.dbi.buffer, 1, &bc);

      //
      // set a completion payload
      //
      struct spn_si_complete_payload * const payload =
        spn_device_dispatch_set_completion(device, impl->id, spn_si_complete, sizeof(*payload));

      payload->impl = impl;

      //
      // submit the dispatch
      //
      spn_device_dispatch_submit(device, impl->id);
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

static spn_result_t
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
      SPN_DEVICE_WAIT(device);
    }

  //
  // wait for any rendering locks to be released
  //
  while (impl->lock_count > 0)
    {
      SPN_DEVICE_WAIT(device);
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

static spn_result_t
spn_si_release(struct spn_styling_impl * const impl)
{
  //
  // was this the last reference?
  //
  // FIXME(allanmac): it's probably wise to change top-level Spinel
  // object reference counts to test for double releases
  //
  if (--impl->styling->ref_count != 0)
    return SPN_SUCCESS;

  struct spn_device * const device = impl->device;

  //
  // wait for any in-flight renders to complete
  //
  while (impl->lock_count > 0)
    {
      SPN_DEVICE_WAIT(device);
    }

  //
  // free device allocations
  //
  if (impl->config->styling.vk.d != 0)
    {
      //
      // Note that we don't have to unmap before freeing
      //
      spn_allocator_device_perm_free(&device->allocator.device.perm.local,
                                     &device->environment,
                                     &impl->vk.d.dbi,
                                     impl->vk.d.dm);
    }

  spn_allocator_device_perm_free(&device->allocator.device.perm.coherent,
                                 &device->environment,
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

spn_result_t
spn_styling_impl_create(struct spn_device * const   device,
                        struct spn_styling ** const styling,
                        uint32_t const              layers_count,
                        uint32_t const              cmds_count)
{
  //
  // FIXME(allanmac): retain the context
  //
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

  struct spn_vk_target_config const * const config = spn_vk_get_config(device->instance);

  impl->config = config;

  impl->lock_count = 0;

  //
  // initialize styling
  //
  s->seal    = spn_si_seal;
  s->unseal  = spn_si_unseal;
  s->release = spn_si_release;

  s->layers.count = layers_count;

  uint32_t const layers_dwords = layers_count * SPN_STYLING_LAYER_COUNT_DWORDS;
  uint32_t const dwords_count  = layers_dwords + cmds_count;

  s->dwords.count = dwords_count;
  s->dwords.next  = layers_dwords;

  s->ref_count = 1;

  //
  // initialize styling extent
  //
  size_t const styling_size = dwords_count * sizeof(uint32_t);

  spn_allocator_device_perm_alloc(&device->allocator.device.perm.coherent,
                                  &device->environment,
                                  dwords_count * sizeof(uint32_t),
                                  NULL,
                                  &impl->vk.h.dbi,
                                  &impl->vk.h.dm);

  vk(MapMemory(device->environment.d, impl->vk.h.dm, 0, VK_WHOLE_SIZE, 0, (void **)&s->extent));

  if (config->styling.vk.d != 0)
    {
      spn_allocator_device_perm_alloc(&device->allocator.device.perm.local,
                                      &device->environment,
                                      styling_size,
                                      NULL,
                                      &impl->vk.d.dbi,
                                      &impl->vk.d.dm);
    }
  else
    {
      impl->vk.d.dbi = impl->vk.h.dbi;
      impl->vk.d.dm  = impl->vk.h.dm;
    }

  //
  // the styling impl starts out unsealed
  //
  SPN_ASSERT_STATE_INIT(impl, SPN_SI_STATE_UNSEALED);

  return SPN_SUCCESS;
}

//
//
//

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

//
//
//

void
spn_styling_happens_before(struct spn_styling * const styling, spn_dispatch_id_t const id)
{
  struct spn_styling_impl * const impl = styling->impl;

  assert(impl->state >= SPN_SI_STATE_SEALING);

  //
  // retain the styling
  //
  spn_si_retain_and_lock(impl);

  //
  // already sealed?
  //
  if (impl->state == SPN_SI_STATE_SEALED)
    return;

  //
  // otherwise... styling happens before render
  //
  spn_device_dispatch_happens_after(impl->device,
                                    id,         // after
                                    impl->id);  // before
}

//
//
//

void
spn_styling_pre_render_bind_ds(struct spn_styling * const         styling,
                               struct spn_vk_ds_styling_t * const ds,
                               VkCommandBuffer                    cb)
{
  struct spn_styling_impl * const impl     = styling->impl;
  struct spn_device * const       device   = impl->device;
  struct spn_vk * const           instance = device->instance;

  assert(impl->state >= SPN_SI_STATE_SEALING);

  //
  // acquire STYLING descriptor set
  //
  spn_vk_ds_acquire_styling(instance, device, ds);

  // copy the dbi structs
  *spn_vk_ds_get_styling_styling(instance, *ds) = impl->vk.d.dbi;

  // update ds
  spn_vk_ds_update_styling(instance, &device->environment, *ds);

  // bind
  spn_vk_ds_bind_render_styling(instance, cb, *ds);
}

//
//
//

void
spn_styling_post_render(struct spn_styling * const styling)
{
  spn_styling_unlock_and_release(styling->impl);
}

//
//
//
