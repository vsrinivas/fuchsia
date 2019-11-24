// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_BUFFER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_BUFFER_H_

#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// A convenience struct used to easily allocate and deallocate Vulkan
// memory buffers (and its device memory) during testing.
typedef struct vk_buffer
{
  VkBuffer                      buffer;
  VkDeviceMemory                memory;
  VkDeviceSize                  size;
  void *                        mapped;
  VkDevice                      device;
  const VkAllocationCallbacks * allocator;

  // fields below are for debugging.
  VkMemoryRequirements memory_requirements;
  uint32_t             memory_type_index;
} vk_buffer_t;

// Generic function to allocate a new GPU buffer and associated memory.
// Using one of the convenience functions below, instead of this one, is
// recommended.
//
// |buffer| is the target vk_buffer_t instance to initialize.
// |buffer_size| is the desired buffer size. The actual allocation size
// will be available as |buffer->size|.
// |buffer_usage|, |memory_flags| and |memory_properties| are used to describe
// the intended buffer usage and memory type for this allocation.
// |queue_families| is either NULL, or an array of |queue_families_count|
// queue family indices that this buffer will be used with.
// |physical_device| is the Vulkan physical device handle for |device|.
// |device| is the Vulkan device instance to allocate from.
// |allocator| is the optional Vulkan host allocator.
//
// NOTE: This function aborts if the allocation cannot succeed. On return,
// the |buffer| fields will be filled appropriately. Any host-visible buffer
// is also automatically mapped by the function for convenience, and its
// address will be available as |buffer.mapped|.
extern void
vk_buffer_alloc_generic(vk_buffer_t *                 buffer,
                        VkDeviceSize                  buffer_size,
                        VkBufferUsageFlags            buffer_usage,
                        VkMemoryPropertyFlags         memory_flags,
                        uint32_t                      queue_families_count,
                        const uint32_t *              queue_families,
                        VkPhysicalDevice              physical_device,
                        VkDevice                      device,
                        const VkAllocationCallbacks * allocator);

// Allocate a new host-visible buffer and map it.
// Assumes the buffer will only every be used by a single queue.
//
// |buffer| is the target vk_buffer_t instance.
// |buffer_size| is the desired memory size, which will be rounded up
// according to specific memory requirements.
// |buffer_usage| describes the buffer's intended usage.
// |physical_device| is the Vulkan physical device handle for |device|.
// |device| is the Vulkan device instance to allocate from.
// |allocator| is the optional Vulkan host allocator.
extern void
vk_buffer_alloc_host(vk_buffer_t *                 buffer,
                     VkDeviceSize                  buffer_size,
                     VkBufferUsageFlags            buffer_usage,
                     VkPhysicalDevice              physical_device,
                     VkDevice                      device,
                     const VkAllocationCallbacks * allocator);

// Allocate a new host-visible, cached and coherent buffer and map it.
// Assumes the buffer will only every be used by a single queue.
//
// |buffer| is the target vk_buffer_t instance.
// |buffer_size| is the desired memory size, which will be rounded up
// according to specific memory requirements.
// |buffer_usage| describes the buffer's intended usage.
// |physical_device| is the Vulkan physical device handle for |device|.
// |device| is the Vulkan device instance to allocate from.
// |allocator| is the optional Vulkan host allocator.
extern void
vk_buffer_alloc_host_coherent(vk_buffer_t *                 buffer,
                              VkDeviceSize                  buffer_size,
                              VkBufferUsageFlags            buffer_usage,
                              VkPhysicalDevice              physical_device,
                              VkDevice                      device,
                              const VkAllocationCallbacks * allocator);

// Allocate a new device-local buffer.
// Assumes the buffer will only ever be used by a single queue.
//
// |buffer| is the target vk_buffer_t instance.
// |buffer_size| is the desired memory size, which will be rounded up
// according to specific memory requirements.
// |buffer_usage| describes the buffer's intended usage.
// |physical_device| is the Vulkan physical device handle for |device|.
// |device| is the Vulkan device instance to allocate from.
// |allocator| is the optional Vulkan host allocator.
extern void
vk_buffer_alloc_device_local(vk_buffer_t *                 buffer,
                             VkDeviceSize                  buffer_size,
                             VkBufferUsageFlags            buffer_usage,
                             VkPhysicalDevice              physical_device,
                             VkDevice                      device,
                             const VkAllocationCallbacks * allocator);

// Flush full content of a buffer. This only makes sense for host-visible
// buffers that are not coherent.
extern void
vk_buffer_flush_all(const vk_buffer_t * buffer);

// Release a buffer and its memory.
extern void
vk_buffer_free(vk_buffer_t * buffer);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_BUFFER_H_
