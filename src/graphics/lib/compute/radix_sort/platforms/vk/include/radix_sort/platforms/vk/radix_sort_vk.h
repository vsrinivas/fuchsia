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
// Radix Sort Vk is a high-performance sorting library for Vulkan 1.2.
//
// The sorting function is both directly and indirectly dispatchable.
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
// Get a Radix Sort target's Vulkan requirements.
//
// A Radix Sort target is a binary image containing configuration parameters and
// a bundle of SPIR-V modules.
//
// Targets are prebuilt and specific to a particular device vendor, architecture
// and key-val configuration.
//
// A Radix Sort instance can only be created with a VkDevice that is initialized
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
//   * The .ext_names field is NULL and the number of required extensions is
//     greater than zero.
//   * The .ext_name_count is less than the number of required extensions is
//     greater than zero.
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
// Returns NULL on failure.
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
radix_sort_vk_destroy(struct radix_sort_vk *        rs,  //
                      VkDevice                      d,
                      VkAllocationCallbacks const * ac);

//
// Returns the buffer size and alignment requirements for a maximum number of
// keyvals.
//
// The radix sort implementation is not an in-place sorting algorithm so two
// non-overlapping keyval buffers are required that are at least
// `.keyvals_size`.
//
// The radix sort instance also requires an `internal` buffer during sorting.
//
// If the indirect dispatch sorting function is used, then an `indirect` buffer
// is also required.
//
// The alignment requirements for the keyval, internal, and indirect buffers
// must be honored.  All alignments are power of 2.
//
//   Input:
//     count             : Maximum number of keyvals
//
//   Outputs:
//     keyval_size       : Size of a single keyval
//
//     keyvals_size      : Minimum size of the even and odd keyval buffers
//     keyvals_alignment : Alignment of each keyval buffer
//
//     internal_size     : Minimum size of internal buffer
//     internal_aligment : Alignment of the internal buffer
//
//     indirect_size     : Minimum size of indirect buffer
//     indirect_aligment : Alignment of the indirect buffer
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
//   VK_BUFFER_USAGE_TRANSFER_DST_BIT ("direct" mode only)
//
//   .indirect
//   ---------
//   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
//   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
//   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
//

struct radix_sort_vk_memory_requirements
{
  VkDeviceSize keyval_size;

  VkDeviceSize keyvals_size;
  VkDeviceSize keyvals_alignment;

  VkDeviceSize internal_size;
  VkDeviceSize internal_alignment;

  VkDeviceSize indirect_size;
  VkDeviceSize indirect_alignment;
};

void
radix_sort_vk_get_memory_requirements(struct radix_sort_vk const *               rs,
                                      uint32_t                                   count,
                                      struct radix_sort_vk_memory_requirements * mr);

//
// Direct dispatch sorting
// -----------------------
//
// Using a key size of `key_bits`, sort `count` keyvals found in the
// `.devaddr_keyvals_even` buffer.
//
// Each internal sorting pass copies the keyvals from one keyvals buffer to the
// other.
//
// The number of internal sorting passes is determined by `.key_bits`.
//
// If an even number of internal sorting passes is required, the sorted keyvals
// will be found in the "even" keyvals buffer.  Otherwise, the sorted keyvals
// will be found in the "odd" keyvals buffer.
//
// Which buffer has the sorted keyvals is returned in `keyvals_sorted`.
//
// A keyval's `key_bits` are the most significant bits of a keyval.
//
// The maximum number of key bits is determined by the keyval size.
//
// The keyval count must be less than (1 << 30) as well as be less than or equal
// to the count used to obtain the the memory requirements.
//
// The info struct's `ext` member must be NULL.
//
// This function appends push constants, dispatch commands, and barriers.
//
// Pipeline barriers should be applied as necessary, both before and after
// invoking this function.
//
// The sort begins with either a TRANSFER/WRITE or a COMPUTE/READ to the
// `internal` and `keyvals_even` buffers.
//
// The sort ends with a COMPUTE/WRITE to the `internal` and `keyvals_sorted`
// buffers.
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
radix_sort_vk_sort(struct radix_sort_vk const *           rs,
                   struct radix_sort_vk_sort_info const * info,
                   VkDevice                               device,
                   VkCommandBuffer                        cb,
                   VkDescriptorBufferInfo *               keyvals_sorted);

//
// Indirect dispatch sorting
// -------------------------
//
// Using a key size of `key_bits`, at pipeline execution time, load keyvals
// count from `devaddr_count` and sorts the keyvals in `.devaddr_keyvals_even`.
//
// Each internal sorting pass copies the keyvals from one keyvals buffer to the
// other.
//
// The number of internal sorting passes is determined by `.key_bits`.
//
// If an even number of internal sorting passes is required, the sorted keyvals
// will be found in the "even" keyvals buffer.  Otherwise, the sorted keyvals
// will be found in the "odd" keyvals buffer.
//
// Which buffer has the sorted keyvals is returned in `keyvals_sorted`.
//
// A keyval's `key_bits` are the most significant bits of a keyval.
//
// The keyval count must be less than (1 << 30) as well as be less than or equal
// to the count used to obtain the the memory requirements.
//
// The info struct's `ext` member must be NULL.
//
// This function appends push constants, dispatch commands, and barriers.
//
// Pipeline barriers should be applied as necessary, both before and after
// invoking this function.
//
// The indirect radix sort begins with a COMPUTE/READ from the `count` buffer
// and ends with a COMPUTE/WRITE to the `internal` and the `keyvals_sorted`
// buffers.
//
// The `indirect` buffer must support USAGE_INDIRECT.
//
// The `count` buffer must be at least 4 bytes and 4-byte aligned.
//

struct radix_sort_vk_sort_indirect_info
{
  void *                         ext;
  uint32_t                       key_bits;
  VkDescriptorBufferInfo const * count;
  VkDescriptorBufferInfo const * keyvals_even;
  VkDescriptorBufferInfo const * keyvals_odd;
  VkDescriptorBufferInfo const * internal;
  VkDescriptorBufferInfo const * indirect;
};

void
radix_sort_vk_sort_indirect(struct radix_sort_vk const *                    rs,
                            struct radix_sort_vk_sort_indirect_info const * info,
                            VkDevice                                        device,
                            VkCommandBuffer                                 cb,
                            VkDescriptorBufferInfo *                        keyvals_sorted);

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
