// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_ALLOCATOR_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_ALLOCATOR_H_

//
// All device memory allocations are either long-lasting or
// short-lived and are made via the functions below.
//
// Once a Spinel instance is created, its *internal* allocations are
// short-lived and acquired from a suballocator.
//
// External-facing APIs like the path/raster builders and compositions
// acquire long-lived memory allocations.
//

#include <vulkan/vulkan_core.h>

#include "spinel/platforms/vk/spinel_vk_types.h"

//
//
//
#define SPN_ALLOCATOR_MAX_QUEUE_FAMILY_INDICES 2

//
//
//
struct spinel_device;

//
//
//
struct spinel_allocator
{
  VkMemoryPropertyFlags properties;
  VkBufferUsageFlags    usage;
  VkSharingMode         mode;
  uint32_t              queue_family_count;
  uint32_t              queue_family_indices[SPN_ALLOCATOR_MAX_QUEUE_FAMILY_INDICES];
};

//
// DBI structures
//
struct spinel_dbi_dm
{
  VkDescriptorBufferInfo dbi;
  VkDeviceMemory         dm;
};

struct spinel_dbi_dm_devaddr
{
  struct spinel_dbi_dm dbi_dm;
  VkDeviceAddress      devaddr;
};

struct spinel_dbi_devaddr
{
  VkDescriptorBufferInfo dbi;
  VkDeviceAddress        devaddr;
};

//
//
//
void
spinel_allocator_create(struct spinel_allocator * allocator,
                        VkMemoryPropertyFlags     properties,
                        VkBufferUsageFlags        usage,
                        VkSharingMode             mode,
                        uint32_t                  queue_family_count,
                        uint32_t const            queue_family_indices[]);

//
//
//
void
spinel_allocator_alloc_dbi_dm(struct spinel_allocator *     allocator,
                              VkPhysicalDevice              pd,
                              VkDevice                      d,
                              VkAllocationCallbacks const * ac,
                              VkDeviceSize                  size,
                              VkDeviceSize *                alignment,
                              struct spinel_dbi_dm *        dbi_dm);

//
//
//
void
spinel_allocator_alloc_dbi_dm_devaddr(struct spinel_allocator *      allocator,
                                      VkPhysicalDevice               pd,
                                      VkDevice                       d,
                                      VkAllocationCallbacks const *  ac,
                                      VkDeviceSize                   size,
                                      VkDeviceSize *                 alignment,
                                      struct spinel_dbi_dm_devaddr * dbi_dm_devaddr);

//
//
//
void
spinel_allocator_free_dbi_dm(struct spinel_allocator *     allocator,
                             VkDevice                      d,
                             VkAllocationCallbacks const * ac,
                             struct spinel_dbi_dm *        dbi_dm);

//
//
//
void
spinel_dbi_devaddr_init_devaddr(VkDevice d, struct spinel_dbi_devaddr * dbi_devaddr);

//
//
//
void
spinel_dbi_dm_devaddr_init_devaddr(VkDevice d, struct spinel_dbi_dm_devaddr * dbi_dm_devaddr);

//
//
//
void
spinel_dbi_devaddr_from_dbi(VkDevice                       d,
                            struct spinel_dbi_devaddr *    dbi_devaddr,
                            VkDescriptorBufferInfo const * dbi,
                            VkDeviceSize                   offset,
                            VkDeviceSize                   range);

//
//
//
VkDeviceAddress
spinel_dbi_to_devaddr(VkDevice d, VkDescriptorBufferInfo const * dbi);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_ALLOCATOR_H_
