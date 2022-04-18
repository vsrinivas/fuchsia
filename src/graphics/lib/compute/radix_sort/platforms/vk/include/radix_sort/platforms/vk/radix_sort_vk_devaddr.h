// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_INCLUDE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_DEVADDR_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_INCLUDE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_DEVADDR_H_

//
// Provides a (nearly) pure VkDeviceAddress interface to the radix sort library.
//

#include "radix_sort/platforms/vk/radix_sort_vk.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// NOTE(allanmac): It's unlikely your code should be using this public interface
// to the sorting library.
//
// This public header provides an interface to the radix sorting library that
// can ease integration with, for example, a Vulkan implementation.  It also
// prepares code for the arrival of pure device address commands like FILL and
// DISPATCH.
//
// Until there are pure VkDeviceAddress equivalents to vkCmdFillBuffer() and
// vkCmdDispatchIndirect(), this structure is used to capture the remaining
// VkBuffer dependencies.
//
// TODO(allanmac): The "direct" sort function could be updated to entirely
// remove its dependency on vkCmdFillBuffer() by using the same "FILL" compute
// shader used by the "indirect" sort function. But the "indirect" sort function
// is still dependent on the buffer argument to vkCmdDispatchIndirect().
//

//
// This structure has semantics similar to VkDescriptorBufferInfo except that
// it's missing the `.range` member and includes a buffer device address value.
//
// As noted above, this structure serves two purposes:
//
//   * Bridge missing functionality in Vulkan.  Namely, a number of older
//     commands do not yet have pure device address equivalents while recently
//     added commands (e.g. "acceleration" commands) do not depend on VkBuffer
//     arguments.
//
//   * Integrate with libraries that might be *below* the public Vulkan API. In
//     this case, the .buffer and .offset values would likely be ignored and
//     driver-internal FILL and DISPATCH functions would accept device
//     addresses.
//
typedef struct radix_sort_vk_buffer_info
{
  VkBuffer        buffer;   // See VkDescriptorBufferInfo.buffer
  VkDeviceSize    offset;   // See VkDescriptorBufferInfo.range
  VkDeviceAddress devaddr;  // vkGetBufferDeviceAddress(.buffer) + .offset
} radix_sort_vk_buffer_info_t;

//
// Function prototypes
//

//
// An implementation of this function must match the semantics of
// vkCmdFillBuffer().
//
// The implementation fills `size` bytes with a value of `data` starting at
// `buffer_info->devaddr` + `offset`.
//
typedef void (*radix_sort_vk_fill_buffer_pfn)(VkCommandBuffer                     cb,
                                              radix_sort_vk_buffer_info_t const * buffer_info,
                                              VkDeviceSize                        offset,
                                              VkDeviceSize                        size,
                                              uint32_t                            data);
//
// An implementation of this function must match the semantics of
// vkCmdDispatchIndirect().
//
// The dispatch loads its VkDispatchIndirectCommand parameters from
// `buffer_info->devaddr`.
//
typedef void (*radix_sort_vk_dispatch_indirect_pfn)(VkCommandBuffer                     cb,
                                                    radix_sort_vk_buffer_info_t const * buffer_info,
                                                    VkDeviceSize                        offset);

//
// Direct dispatch sorting using buffer device addresses
// -----------------------------------------------------
//
typedef struct radix_sort_vk_sort_devaddr_info
{
  void *                        ext;
  uint32_t                      key_bits;
  uint32_t                      count;
  radix_sort_vk_buffer_info_t   keyvals_even;
  VkDeviceAddress               keyvals_odd;
  radix_sort_vk_buffer_info_t   internal;
  radix_sort_vk_fill_buffer_pfn fill_buffer_pfn;
} radix_sort_vk_sort_devaddr_info_t;

void
radix_sort_vk_sort_devaddr(radix_sort_vk_t const *                   rs,
                           radix_sort_vk_sort_devaddr_info_t const * info,
                           VkDevice                                  device,
                           VkCommandBuffer                           cb,
                           VkDeviceAddress *                         keyvals_sorted);

//
// Indirect dispatch sorting using buffer device addresses
// -------------------------------------------------------
//
// clang-format off
//
typedef struct radix_sort_vk_sort_indirect_devaddr_info
{
  void *                              ext;
  uint32_t                            key_bits;
  VkDeviceAddress                     count;
  VkDeviceAddress                     keyvals_even;
  VkDeviceAddress                     keyvals_odd;
  VkDeviceAddress                     internal;
  radix_sort_vk_buffer_info_t         indirect;
  radix_sort_vk_dispatch_indirect_pfn dispatch_indirect_pfn;
} radix_sort_vk_sort_indirect_devaddr_info_t;

void
radix_sort_vk_sort_indirect_devaddr(radix_sort_vk_t const *                            rs,
                                    radix_sort_vk_sort_indirect_devaddr_info_t const * info,
                                    VkDevice                                           device,
                                    VkCommandBuffer                                    cb,
                                    VkDeviceAddress *                                  keyvals_sorted);

//
// clang-format on
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_INCLUDE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_DEVADDR_H_
