// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_buffer.h"

#include "tests/common/utils.h"     // For ASSERT() macros.
#include "tests/common/vk_utils.h"  // For vk() macro.

void
vk_buffer_alloc_generic(vk_buffer_t *                 buffer,
                        VkDeviceSize                  buffer_size,
                        VkBufferUsageFlags            usage,
                        VkMemoryPropertyFlags         memory_flags,
                        uint32_t                      queue_families_count,
                        const uint32_t *              queue_families,
                        VkPhysicalDevice              physical_device,
                        VkDevice                      device,
                        const VkAllocationCallbacks * allocator)
{
  // First create a buffer that can be used as a transfer source for our
  // application.
  VkBufferCreateInfo createInfo = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .size  = buffer_size,
    .usage = usage,

    // NOTE: If the buffer was to be accessed from different queues
    // at the same time, sharingMode should be VK_SHARING_MODE_EXCLUSIVE
    // and the family queue indices should be listed through
    // queueFamilyIndexCount and pQueueFamilyIndices...
    .sharingMode =
      (queue_families_count > 0) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,

    .queueFamilyIndexCount = queue_families_count,
    .pQueueFamilyIndices   = queue_families,
  };

  vk(CreateBuffer(device, &createInfo, allocator, &buffer->buffer));

  // Get its memory requirements to ensure we have the right memory type.
  VkMemoryRequirements memory_requirements;
  vkGetBufferMemoryRequirements(device, buffer->buffer, &memory_requirements);
  buffer->size                = memory_requirements.size;
  buffer->memory_requirements = memory_requirements;

  // Find the right memory type for this buffer. We want it to be host-visible.
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
  uint32_t memory_type_index = UINT32_MAX;
  {
    for (uint32_t n = 0; n < memory_properties.memoryTypeCount; n++)
      {
        if ((memory_requirements.memoryTypeBits & (1u << n)) == 0)
          continue;

        if ((memory_properties.memoryTypes[n].propertyFlags & memory_flags) == memory_flags)
          {
            memory_type_index = n;
            break;
          }
      }
    ASSERT_MSG(memory_type_index != UINT32_MAX, "Could not find memory type for buffer!\n");
    buffer->memory_type_index = memory_type_index;
  }

  // Allocate memory for our buffer. No need for a custom allocator in our
  // trivial application.
  const VkMemoryAllocateInfo allocateInfo = {
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = memory_requirements.size,
    .memoryTypeIndex = memory_type_index,
  };

  vk(AllocateMemory(device, &allocateInfo, allocator, &buffer->memory));

  // Bind the memory to the buffer.
  vk(BindBufferMemory(device, buffer->buffer, buffer->memory, 0));

  // Map it now!
  if (memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    vk(MapMemory(device, buffer->memory, 0, buffer->size, 0, &buffer->mapped));

  buffer->device    = device;
  buffer->allocator = allocator;
}

void
vk_buffer_alloc_host(vk_buffer_t *                 buffer,
                     VkDeviceSize                  buffer_size,
                     VkBufferUsageFlags            buffer_usage,
                     VkPhysicalDevice              physical_device,
                     VkDevice                      device,
                     const VkAllocationCallbacks * allocator)
{
  VkMemoryPropertyFlags memory_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

  vk_buffer_alloc_generic(buffer,
                          buffer_size,
                          buffer_usage,
                          memory_flags,
                          0,     // queue_families_count,
                          NULL,  // queue_families,
                          physical_device,
                          device,
                          allocator);
}

void
vk_buffer_alloc_host_coherent(vk_buffer_t *                 buffer,
                              VkDeviceSize                  buffer_size,
                              VkBufferUsageFlags            buffer_usage,
                              VkPhysicalDevice              physical_device,
                              VkDevice                      device,
                              const VkAllocationCallbacks * allocator)
{
  VkMemoryPropertyFlags memory_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  vk_buffer_alloc_generic(buffer,
                          buffer_size,
                          buffer_usage,
                          memory_flags,
                          0,     // queue_families_count,
                          NULL,  // queue_families,
                          physical_device,
                          device,
                          allocator);
}

void
vk_buffer_alloc_device_local(vk_buffer_t *                 buffer,
                             VkDeviceSize                  buffer_size,
                             VkBufferUsageFlags            buffer_usage,
                             VkPhysicalDevice              physical_device,
                             VkDevice                      device,
                             const VkAllocationCallbacks * allocator)
{
  VkMemoryPropertyFlags memory_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  vk_buffer_alloc_generic(buffer,
                          buffer_size,
                          buffer_usage,
                          memory_flags,
                          0,     // queue_families_count,
                          NULL,  // queue_families,
                          physical_device,
                          device,
                          allocator);
}

void
vk_buffer_flush_all(const vk_buffer_t * buffer)
{
  if (buffer->mapped)
    {
      vk(FlushMappedMemoryRanges(buffer->device,
                                 1,
                                 &(const VkMappedMemoryRange){
                                   .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                   .pNext  = NULL,
                                   .memory = buffer->memory,
                                   .offset = 0,
                                   .size   = buffer->size,
                                 }));
    }
}

void
vk_buffer_free(vk_buffer_t * buffer)
{
  if (buffer->mapped != NULL)
    {
      vkUnmapMemory(buffer->device, buffer->memory);
      buffer->mapped = NULL;
    }

  vkFreeMemory(buffer->device, buffer->memory, buffer->allocator);
  vkDestroyBuffer(buffer->device, buffer->buffer, buffer->allocator);
  buffer->memory    = VK_NULL_HANDLE;
  buffer->buffer    = VK_NULL_HANDLE;
  buffer->device    = VK_NULL_HANDLE;
  buffer->allocator = NULL;
}
