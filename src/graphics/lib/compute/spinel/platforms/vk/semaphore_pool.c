// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// This is the "basic" fence pool implementation.
//
// A host-OS-optimized platform will work directly with the VkFence
// payloads to avoid scanning for signaled fences.
//

#include "semaphore_pool.h"
#include "device.h"
#include "common/vk/vk_assert.h"

//
// No state at this point
//

#if 0
struct spn_semaphore_pool
{
  ;
};
#endif

//
//
//

void
spn_device_semaphore_pool_create(struct spn_device * const device)
{
  ;
}

void
spn_device_semaphore_pool_dispose(struct spn_device * const device)
{
  ;
}

//
//
//

VkSemaphore
spn_device_semaphore_pool_acquire(struct spn_device * const device)
{
  VkSemaphoreCreateInfo const sci = {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0
  };

  struct spn_device_vk * const device_vk = device->vk;

  VkSemaphore semaphore;

  vk(CreateSemaphore(device_vk->d,
                     &sci,
                     device_vk->ac,
                     &semaphore));

  return semaphore;
}

void
spn_device_semaphore_pool_release(struct spn_device * const device,
                                  VkSemaphore         const semaphore)
{
  struct spn_device_vk * const device_vk = device->vk;

  vkDestroySemaphore(device_vk->d,
                     semaphore,
                     device_vk->ac);
}

//
//
//
