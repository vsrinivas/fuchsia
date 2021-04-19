// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_INCLUDE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_INCLUDE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
// Radix Sort is a high-performance GPU-accelerated sorting library for Vulkan
// that supports both direct and execution-time (indirect) dispatch.
//

#ifdef __cplusplus
extern "C" {
#endif

//
// RADIX SORT TYPES
//

struct radix_sort_vk_target;

struct radix_sort_vk_target_requirements
{
  uint32_t                           ext_name_count;
  char const **                      ext_names;
  VkPhysicalDeviceFeatures *         pdf;
  VkPhysicalDeviceVulkan11Features * pdf11;
  VkPhysicalDeviceVulkan12Features * pdf12;
};

//
// RADIX SORT TARGET REQUIREMENTS: VULKAN
//
// A Radix Sort target is a binary image containing target-specific
// configuration parameters and a bundle of SPIR-V modules.
//
// Targets are prebuilt and specific to a particular device vendor, architecture
// and key-val configuration.
//
// A Radix Sort instance must be created with a VkDevice that is initialized
// with all of the target's required extensions and features.
//
// The `radix_sort_vk_target_get_requirements()` function yields the extensions
// and initialized feature flags required by a Radix Sort target.
//
// These requirements can be merged with other Vulkan library requirements
// before VkDevice creation.
//
// If the `.ext_names` member is NULL, the `.ext_name_count` member will be
// initialized.
//
// Returns `false` if:
//
//   * The .ext_names field is NULL and .ext_name_count > 0
//   * The .ext_name_count member is too small
//   * Any of the .pdf, .pdf11 or .pdf12 members are NULL.
//
// Otherwise, returns true.
//

bool
radix_sort_vk_target_get_requirements(struct radix_sort_vk_target const *        target,
                                      struct radix_sort_vk_target_requirements * requirements);

//
// Create a Radix Sort instance for a target.
//
// Keyval size is implicitly determined by the target.
//

struct radix_sort_vk *
radix_sort_vk_create(VkDevice                            device,
                     VkAllocationCallbacks const *       ac,
                     VkPipelineCache                     pc,
                     struct radix_sort_vk_target const * target);

//
// Destroy the Radix Sort instance using the same device and allocator used at
// creation.
//

void
radix_sort_vk_destroy(struct radix_sort_vk * rs, VkDevice device, VkAllocationCallbacks const * ac);

//
// Returns the VkBuffer size and alignment requirements for a maximum number of
// keyvals.
//
// The radix sort implementation is not in place so two non-overlapping keyval
// extents are required that are at least `.keyvals_size`.
//
// The radix sort instance also requires an internal extent during sorting.
//
// The alignment requirements for the keyval and internal extents must be
// honored.
//
//   Input:
//     count             : Maximum number of keyvals
//
//   Outputs:
//     keyval_size       : Size of a single keyval
//
//     keyvals_size      : Minimum size of the even and odd keyval extents
//     keyvals_alignment : Alignment of both keyval extents
//
//     internal_size     : Minimum size of internal extent
//     internal_aligment : Alignment of the internal extent
//
//
// Direct dispatch requires the following buffer usage bits:
//
//   .keyvals_even/odd
//   -----------------
//   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
//   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
//
//   .internal
//   ---------
//   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
//   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
//   VK_BUFFER_USAGE_TRANSFER_DST_BIT
//
// Indirect dispatch additionally requires the INDIRECT bit and assumes
// `.devaddr_count` is 4-byte aligned:
//
//   .internal
//   ---------
//   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
//   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
//   VK_BUFFER_USAGE_TRANSFER_DST_BIT
//   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
//

struct radix_sort_vk_memory_requirements
{
  VkDeviceSize keyval_size;

  VkDeviceSize keyvals_size;
  VkDeviceSize keyvals_alignment;

  VkDeviceSize internal_size;
  VkDeviceSize internal_alignment;
};

void
radix_sort_vk_get_memory_requirements(struct radix_sort_vk const *               rs,
                                      uint32_t                                   count,
                                      struct radix_sort_vk_memory_requirements * mr);

//
// Direct dispatch sorting (default)
// ---------------------------------
//
// Using a key size of `key_bits`, sort `count` keyvals found in the
// `.devaddr_keyvals_even` extent.
//
// Each internal sorting pass copies the keyvals from one extent to the other.
//
// If an even number of internal sorting passes are required, the sorted keyvals
// will be found in the "even" keyvals extent.  Otherwise, the sorted keyvals
// will be found in the "odd" keyvals extent.
//
// Which extent has the sorted keyvals is returned in `keyvals_out`.
//
// A keyval's `key_bits` are the most significant bits of a keyval.
//
// The maximum number of key bits is determined by the keyval size.
//
// The keyval count must be less than RADIX_SORT_VK_MAX_KEYVALS as well as be
// less than or equal to the count used to obtain the the memory requirements.
//
// This function appends push constants, dispatch commands, and barriers.
//
// Pipeline barriers should be applied as necessary, both before and after
// invoking this function.
//
// The radix sort begins with TRANSFER/WRITE and ends with a COMPUTE/WRITE to a
// storage buffer.
//

struct radix_sort_vk_sort_info
{
  void *                         ext;
  uint32_t                       key_bits;
  uint32_t                       count;
  VkDescriptorBufferInfo const * keyvals_even;
  VkDescriptorBufferInfo const * keyvals_odd;
  VkDescriptorBufferInfo const * internal;
};

void
radix_sort_vk_sort(VkDevice                               device,
                   VkCommandBuffer                        cb,
                   struct radix_sort_vk const *           rs,
                   struct radix_sort_vk_sort_info const * info,
                   VkDescriptorBufferInfo const **        keyvals_sorted);

//
// Indirect dispatch sorting
// -------------------------
//
// Using a key size of `key_bits`, at pipeline execution time, load keyvals
// count from `devaddr_count` and sorts the keyvals in `.devaddr_keyvals_even`.
//
// Each internal sorting pass copies the keyvals from one extent to the other.
//
// If an even number of internal sorting passes are required, the sorted keyvals
// will be found in the "even" keyvals extent.  Otherwise, the sorted keyvals
// will be found in the "odd" keyvals extent.
//
// Which extent has the sorted keyvals is returned in `keyvals_out`.
//
// A keyval's `key_bits` are the most significant bits of a keyval.
//
// The maximum number of key bits is determined by the keyval size.
//
// The keyval count must be less than RADIX_SORT_VK_MAX_KEYVALS as well as be
// less than or equal to the count used to obtain the the memory requirements.
//
// This function appends push constants, dispatch commands, and barriers.
//
// Pipeline barriers should be applied as necessary, both before and after
// invoking this function.
//
// The radix sort begins with TRANSFER/WRITE and ends with a COMPUTE/WRITE to a
// storage buffer.
//

struct radix_sort_vk_sort_indirect_info
{
  void *                         ext;
  uint32_t                       key_bits;
  VkDescriptorBufferInfo const * count;
  VkDescriptorBufferInfo const * keyvals_even;
  VkDescriptorBufferInfo const * keyvals_odd;
  VkDescriptorBufferInfo const * internal;
};

void
radix_sort_vk_sort_indirect(VkDevice                                        device,
                            VkCommandBuffer                                 cb,
                            struct radix_sort_vk const *                    rs,
                            struct radix_sort_vk_sort_indirect_info const * info,
                            VkDescriptorBufferInfo const **                 keyvals_sorted);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_RADIX_SORT_PLATFORMS_VK_INCLUDE_RADIX_SORT_PLATFORMS_VK_RADIX_SORT_VK_H_
