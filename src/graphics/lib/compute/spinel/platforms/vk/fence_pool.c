// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// This is the "basic" fence pool implementation.
//
// A host-OS-optimized platform will work directly with the VkFence
// payloads to avoid scanning for signaled fences.
//

#include "fence_pool.h"

#include <string.h>

#include "cb_pool.h"
#include "common/macros.h"
#include "common/vk/vk_assert.h"
#include "device.h"

//
// Note that this is reentrant but single-threaded (for now)
//

struct spn_fence_cb
{
  struct spn_fence_cb *  next;
  VkCommandBuffer        cb;
  VkFence                fence;
  spn_fence_complete_pfn pfn;
  uint8_t                pfn_payload[SPN_FENCE_COMPLETE_PFN_PAYLOAD_SIZE_MAX];
};

//
//
//

struct spn_fence_pool
{
  struct spn_fence_cb * cbs;

  struct spn_fence_cb * unsignaled;
  struct spn_fence_cb * available;

  struct
  {
    VkFence * extent;
    uint32_t  count;
  } fences;
};

STATIC_ASSERT_MACRO_1(sizeof(struct spn_fence_pool) % sizeof(void *) == 0);

//
//
//

void
spn_device_fence_pool_create(struct spn_device * const device, uint32_t const size)
{
  assert(size >= 1);

  //
  // allocate
  //
  struct spn_fence_pool * fence_pool = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                                     SPN_MEM_FLAGS_READ_WRITE,
                                                                     sizeof(*fence_pool));

  device->fence_pool = fence_pool;

  fence_pool->cbs = spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                                  SPN_MEM_FLAGS_READ_WRITE,
                                                  sizeof(*fence_pool->cbs) * size);

  fence_pool->fences.extent =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*fence_pool->fences.extent) * size);
  //
  // initialize links
  //
  VkFenceCreateInfo const fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                 .pNext = NULL,
                                 .flags = 0};

  uint32_t              rem  = size;
  struct spn_fence_cb * curr = fence_pool->cbs;

  while (true)
    {
      vk(CreateFence(device->environment->d, &fci, device->environment->ac, &curr->fence));

      if (--rem == 0)
        break;

      curr->next = curr + 1;
      curr       = curr + 1;
    }

  curr->next = NULL;

  //
  // initialize heads
  //
  fence_pool->unsignaled = NULL;
  fence_pool->available  = fence_pool->cbs;
}

//
//
//

void
spn_device_fence_pool_dispose(struct spn_device * const device)
{
  // destroy all VkFences
  struct spn_fence_pool * const fence_pool = device->fence_pool;
  struct spn_fence_cb *         curr       = fence_pool->available;

  VkAllocationCallbacks const * const vk_ac = device->environment->ac;
  VkDevice                            vk_d  = device->environment->d;

  do
    {
      vkDestroyFence(vk_d, curr->fence, vk_ac);

      curr = curr->next;
    }
  while (curr != NULL);

  //
  // FIXME -- interrupt and free VkFences
  //
  spn_allocator_host_perm_free(&device->allocator.host.perm, fence_pool->cbs);

  spn_allocator_host_perm_free(&device->allocator.host.perm, fence_pool->fences.extent);

  spn_allocator_host_perm_free(&device->allocator.host.perm, fence_pool);
}

//
//
//

static void
spn_device_fence_pool_drain(struct spn_device * const     device,
                            struct spn_fence_pool * const fence_pool,
                            struct spn_fence_cb *         signaled)
{
  //
  // Even though the fence_pool is single-threaded there is some
  // subtlety in properly draining the signaled list of completion
  // routines.
  //
  // Most of the subtlety is driven by acquire().  This function
  // assumes that if there are no available completion records then
  // there *must* be unsignaled completions.
  //
  // This invariant demands that a signaled completion record is made
  // available *before* invoking its pfn.  This requires either:
  //
  //   1. Making a local copy of the payload before invocation.
  //
  //   2. The invoked completion routine makes a local copy of payload
  //      members before possibly relinqushing control via a
  //      spn_device_yield/wait/drain().
  //
  // For now, we are depending on the (subtler) option 2.
  //
  do
    {
      // release the cb
      spn_device_cb_pool_release(device, signaled->cb);

      // reset the fence
      vk(ResetFences(device->environment->d, 1, &signaled->fence));

      struct spn_fence_cb * const curr = signaled;

      signaled              = signaled->next;
      curr->next            = fence_pool->available;
      fence_pool->available = curr;

      // invoke callback
      if (curr->pfn != NULL)
        {
          //
          // FENCE_POOL INVARIANT:
          //
          // COMPLETION ROUTINE MUST MAKE LOCAL COPIES OF PAYLOAD BEFORE
          // ANY POTENTIAL INVOCATION OF SPN_DEVICE_YIELD/WAIT/DRAIN()
          //
          curr->pfn(curr->pfn_payload);
        }
    }
  while (signaled != NULL);
}

//
// vkWaitForFences() needs an array of VkFence handles
//

static void
spn_fence_pool_regenerate_array(struct spn_fence_pool * const fence_pool,
                                struct spn_fence_cb *         next)
{
  uint32_t count = 0;

  if (next != NULL)
    {
      VkFence * fences = fence_pool->fences.extent;

      do
        {
          fences[count++] = next->fence;
          next            = next->next;
        }
      while (next != NULL);
    }

  fence_pool->fences.count = count;
}

//
// Must always be guarded with a test for .unsignaled != NULL
//

static void
spn_device_fence_pool_wait(struct spn_device * const     device,
                           struct spn_fence_pool * const fence_pool,
                           uint64_t                      timeout_ns)
{
  // regenerate the array of fences
  spn_fence_pool_regenerate_array(fence_pool, fence_pool->unsignaled);

  //
  // wait for signaled or timeout
  //
  VkDevice vk_d = device->environment->d;

  {
    VkResult const res =
      vkWaitForFences(vk_d, fence_pool->fences.count, fence_pool->fences.extent, false, timeout_ns);
    switch (res)
      {
        case VK_SUCCESS:
          break;

        case VK_TIMEOUT:
          return;

        default:
          spn_device_lost(device);
          return;
      }
  }

  //
  // collect signaled... note that unsignaled may be reordered
  //
  struct spn_fence_cb * curr     = fence_pool->unsignaled;
  struct spn_fence_cb * signaled = NULL;

  fence_pool->unsignaled = NULL;

  do
    {
      VkResult const res = vkGetFenceStatus(vk_d, curr->fence);

      switch (res)
        {
            case VK_SUCCESS: {
              struct spn_fence_cb * const next = curr->next;
              curr->next                       = signaled;
              signaled                         = curr;
              curr                             = next;
              break;
            }
            case VK_NOT_READY: {
              struct spn_fence_cb * const next = curr->next;
              curr->next                       = fence_pool->unsignaled;
              fence_pool->unsignaled           = curr;
              curr                             = next;
              break;
            }
          default:
            spn_device_lost(device);
            return;
        }
    }
  while (curr != NULL);

  //
  // drain signaled...
  //
  if (signaled != NULL)
    {
      spn_device_fence_pool_drain(device, fence_pool, signaled);
    }
}

//
// FIXME -- need to surface fatal VK errors
//

spn_result
spn_device_yield(struct spn_device * const device)
{
  struct spn_fence_pool * const fence_pool = device->fence_pool;

  // anything to do?
  if (fence_pool->unsignaled == NULL)
    return SPN_SUCCESS;

  spn_device_fence_pool_wait(device, fence_pool, 0UL);

  return SPN_SUCCESS;
}

spn_result
spn_device_wait(struct spn_device * const device)
{
  struct spn_fence_pool * const fence_pool = device->fence_pool;

  // anything to do?
  if (fence_pool->unsignaled == NULL)
    return SPN_SUCCESS;

  spn_device_fence_pool_wait(device, fence_pool, spn_device_wait_nsecs(device));

  return SPN_SUCCESS;
}

spn_result
spn_device_drain(struct spn_device * const device)
{
  struct spn_fence_pool * const fence_pool = device->fence_pool;

  // anything to do?
  if (fence_pool->unsignaled == NULL)
    return SPN_SUCCESS;

  uint64_t const timeout_ns = spn_device_wait_nsecs(device);

  do
    {
      spn_device_fence_pool_wait(device, fence_pool, timeout_ns);
    }
  while (fence_pool->unsignaled != NULL);

  return SPN_SUCCESS;
}

//
//
//

VkFence
spn_device_fence_pool_acquire(struct spn_device * const    device,
                              VkCommandBuffer const        cb,
                              spn_fence_complete_pfn const pfn,
                              void * const                 pfn_payload,
                              size_t const                 pfn_payload_size)
{
  assert(pfn_payload_size <= SPN_FENCE_COMPLETE_PFN_PAYLOAD_SIZE_MAX);

  struct spn_fence_pool * const fence_pool = device->fence_pool;
  struct spn_fence_cb *         head;

  // anything to do?
  if ((head = fence_pool->available) == NULL)
    {
      uint64_t const timeout_ns = spn_device_wait_nsecs(device);

      do
        {
          spn_device_fence_pool_wait(device, fence_pool, timeout_ns);
        }
      while ((head = fence_pool->available) == NULL);
    }

  // unlink and relink
  fence_pool->available  = head->next;
  head->next             = fence_pool->unsignaled;
  fence_pool->unsignaled = head;

  // save cb
  head->cb = cb;

  // save the head pfn
  head->pfn = pfn;

  // copy the pfn payload
  if (pfn_payload_size > 0)
    memcpy(head->pfn_payload, pfn_payload, pfn_payload_size);

  return head->fence;
}

//
//
//
