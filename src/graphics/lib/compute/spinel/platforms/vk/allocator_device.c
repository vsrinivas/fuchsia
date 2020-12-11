// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#ifndef NDEBUG
#include <stdio.h>
#endif

//
//
//

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

//
//
//

#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/find_mem_type_idx.h"
#include "device.h"

//
// Section 11.6 of the Vulkan spec says:
//
//   The VkMemoryRequirements.memoryTypeBits member is identical for
//   all VkBuffer objects created with the same value for the flags
//   and usage members in the VkBufferCreateInfo structure and the
//   handleTypes member of the VkExternalMemoryBufferCreateInfo
//   structure passed to vkCreateBuffer. Further, if usage1 and usage2
//   of type VkBufferUsageFlags are such that the bits set in usage2
//   are a subset of the bits set in usage1, and they have the same
//   flags and VkExternalMemoryBufferCreateInfo ::handleTypes, then
//   the bits set in memoryTypeBits returned for usage1 must be a
//   subset of the bits set in memoryTypeBits returned for usage2, for
//   all values of flags.
//
// This presents some optimization opportunities but unfortunately it
// also results in the validator bleating.
//
// So for now, just capture the VkMemoryPropertyFlags,
// VkBufferUsageFlags and queue family indices in the perm allocator.
//

//
// PERM
//

void
spn_allocator_device_perm_create(struct spn_allocator_device_perm * const device_perm,
                                 struct spn_vk_environment * const        environment,
                                 VkMemoryPropertyFlags const              mpf,
                                 VkBufferUsageFlags const                 buf,
                                 uint32_t const                           queue_family_count,
                                 uint32_t const                           queue_family_indices[])
{
  assert(queue_family_count <= SPN_ALLOCATOR_DEVICE_PERM_MAX_QUEUE_FAMILY_INDICES);

  if (queue_family_count > 0)
    {
      memcpy(device_perm->queue_family_indices,
             queue_family_indices,
             queue_family_count * sizeof(device_perm->queue_family_indices[0]));
    }

  device_perm->queue_family_count = queue_family_count;
  device_perm->mpf                = mpf;
  device_perm->buf                = buf;
}

void
spn_allocator_device_perm_dispose(struct spn_allocator_device_perm * const device_perm,
                                  struct spn_vk_environment * const        environment)
{
  ;
}

void
spn_allocator_device_perm_alloc(struct spn_allocator_device_perm * const device_perm,
                                struct spn_vk_environment * const        environment,
                                VkDeviceSize const                       size,
                                VkDeviceSize * const                     alignment,
                                VkDescriptorBufferInfo * const           dbi,
                                VkDeviceMemory * const                   devmem)
{
  VkBufferCreateInfo const bci = {
    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0,  // only time this will ever change is if we're allocating protected
    .size                  = size,
    .usage                 = device_perm->buf,
    .sharingMode           = ((device_perm->queue_family_count == 0) ? VK_SHARING_MODE_EXCLUSIVE
                                                                     : VK_SHARING_MODE_CONCURRENT),
    .queueFamilyIndexCount = device_perm->queue_family_count,
    .pQueueFamilyIndices   = device_perm->queue_family_indices
  };

  vk(CreateBuffer(environment->d, &bci, environment->ac, &dbi->buffer));

  VkMemoryRequirements mr;

  vkGetBufferMemoryRequirements(environment->d, dbi->buffer, &mr);

  if (alignment != NULL)
    *alignment = mr.alignment;

  dbi->offset = 0;
  dbi->range  = size;  // could be smaller than mr.size

  //
  // FIXME(allanmac): investigate dedicated allocations -- see NVIDIA docs
  //
#if 0
  VkMemoryDedicatedAllocateInfo const dai =
    {
      .sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext  = NULL,
      .image  = VK_NULL_HANDLE,
      .buffer = dbi->buffer
    };
#endif

  //
  // allocate
  //
  struct VkMemoryAllocateInfo const mai = {
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = mr.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&environment->pdmp, mr.memoryTypeBits, device_perm->mpf)
  };

  vk(AllocateMemory(environment->d, &mai, environment->ac, devmem));

  vk(BindBufferMemory(environment->d, dbi->buffer, *devmem, 0));
}

void
spn_allocator_device_perm_free(struct spn_allocator_device_perm * const device_perm,
                               struct spn_vk_environment * const        environment,
                               VkDescriptorBufferInfo * const           dbi,
                               VkDeviceMemory                           devmem)
{
  vkFreeMemory(environment->d, devmem, environment->ac);

  vkDestroyBuffer(environment->d, dbi->buffer, environment->ac);
}

//
// TEMP
//

void
spn_allocator_device_temp_create(struct spn_allocator_device_temp * const device_temp,
                                 struct spn_allocator_host_perm * const   host_perm,
                                 struct spn_allocator_device_perm * const device_perm,
                                 struct spn_vk_environment * const        environment,
                                 uint32_t const                           subbufs,
                                 VkDeviceSize const                       size)
{
  device_temp->host_perm   = host_perm;
  device_temp->device_perm = device_perm;

  VkDeviceSize alignment;

  spn_allocator_device_perm_alloc(device_perm,
                                  environment,
                                  size,
                                  &alignment,
                                  &device_temp->dbi,
                                  &device_temp->devmem);

  spn_suballocator_create(&device_temp->suballocator,
                          host_perm,
                          "DEVICE ",
                          subbufs,
                          size,
                          alignment);
}

void
spn_allocator_device_temp_dispose(struct spn_allocator_device_temp * const device_temp,
                                  struct spn_vk_environment * const        environment)
{
  spn_suballocator_dispose(&device_temp->suballocator, device_temp->host_perm);

  spn_allocator_device_perm_free(device_temp->device_perm,
                                 environment,
                                 &device_temp->dbi,
                                 device_temp->devmem);
}

void
spn_allocator_device_temp_alloc(struct spn_allocator_device_temp * const device_temp,
                                struct spn_device * const                device,
                                spn_suballocator_wait_pfn                wait,
                                VkDeviceSize const                       size,
                                spn_subbuf_id_t * const                  subbuf_id,
                                VkDescriptorBufferInfo * const           subbuf_dbi)
{
  if (size == 0)
    {
      *subbuf_id = SPN_SUBBUF_ID_INVALID;

      subbuf_dbi->buffer = VK_NULL_HANDLE;
      subbuf_dbi->offset = 0;
      subbuf_dbi->range  = 0;

      return;
    }

  subbuf_dbi->buffer = device_temp->dbi.buffer;

  spn_suballocator_subbuf_alloc(&device_temp->suballocator,
                                device,
                                wait,
                                size,
                                subbuf_id,
                                &subbuf_dbi->offset,
                                &subbuf_dbi->range);
}

void
spn_allocator_device_temp_free(struct spn_allocator_device_temp * const device_temp,
                               spn_subbuf_id_t const                    subbuf_id)
{
  spn_suballocator_subbuf_free(&device_temp->suballocator, subbuf_id);
}

//
//
//
