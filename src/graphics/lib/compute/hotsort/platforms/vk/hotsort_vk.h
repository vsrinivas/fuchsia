// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_HOTSORT_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_HOTSORT_VK_H_

//
// NOTE: reevaluate the HotSort/VK API once "Physical Storage Buffer
// Access" is more widely supported.
//

#include <vulkan/vulkan_core.h>

//
//
//

#include <stdbool.h>
#include <stdint.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// HotSort/VK relies on pipeline layout compatibility:
//
//   Push constants:
//
//    - stages : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
//    - offset : 0
//    - size   : 12
//
//   Descriptor sets:
//
//    - Input:
//
//      * storage buffer at layout(set=in.set,binding=in.binding)
//      * key-vals beginning at offset 'in'
//
//    - Output:
//
//      * storage buffer at layout(set=out.set,binding=out.binding)
//      * key-vals beginning at offset 'out'
//
// The locations of the input and output buffers are declared at
// HotSort instance creation.
//
// The buffer offsets can vary with each invocation of hotsort_vk_sort().
//

//
// A HotSort target is an opaque structure containing target-specific
// configuration parameters and a bundle of SPIR-V modules.
//
// Targets are generated and specific to a particular device vendor,
// architecture and key-val configuration.
//

struct hotsort_vk_target;

//
// TARGET REQUIREMENTS: VULKAN
//

struct hotsort_vk_target_requirements
{
  uint32_t                   ext_name_count;
  char const **              ext_names;
  VkPhysicalDeviceFeatures * pdf;
};

//
// Yields the extensions and features required by a HotSort target.
//
// If target is not NULL and requirements.ext_names is NULL then the
// respective count will be initialized.
//
// Returns false if:
//
//   * target is NULL
//   * requirements is NULL
//   * requirements.ext_names is NULL
//   * requirements.pdf is NULL
//   * requirements.ext_name_count is too small
//
// Otherwise returns true.
//

bool
hotsort_vk_target_get_requirements(struct hotsort_vk_target const * const        target,
                                   struct hotsort_vk_target_requirements * const requirements);

//
// HotSort push constants are expected at offset 0
//

struct hotsort_vk_push
{
  uint32_t kv_offset_in;
  uint32_t kv_offset_out;
  uint32_t kv_count;
};

// clang-format off
#define HOTSORT_VK_PUSH_CONSTANT_RANGE_STAGE_FLAGS VK_SHADER_STAGE_COMPUTE_BIT
#define HOTSORT_VK_PUSH_CONSTANT_RANGE_OFFSET      0
#define HOTSORT_VK_PUSH_CONSTANT_RANGE_SIZE        sizeof(struct hotsort_vk_push)
// clang-format on

//
// Declare the offsets of the key-value arrays before sorting.
//

struct hotsort_vk_ds_offsets
{
  VkDeviceSize in;
  VkDeviceSize out;
};

//
// Create a HotSort instance for a target that operates on storage
// buffers at specific descriptor set locations.
//

struct hotsort_vk *
hotsort_vk_create(VkDevice                               device,
                  VkAllocationCallbacks const *          allocator,
                  VkPipelineCache                        pipeline_cache,
                  VkPipelineLayout                       pipeline_layout,
                  struct hotsort_vk_target const * const target);

//
// Resources will be disposed of with the same device and allocator
// used for creation.
//

void
hotsort_vk_release(VkDevice                            device,
                   VkAllocationCallbacks const * const allocator,
                   struct hotsort_vk * const           hs);

//
// Explicitly reveal what padding of maximum valued key-vals will be
// applied to the input and output buffers.
//
//   Input:
//     count      : Input number of key-vals
//
//   Output:
//
//     slabs_in   : Number of slabs for input key-vals
//     padded_in  : Padded number of input key-vals
//     padded_out : Padded number of output key-vals
//
// Instead of implicitly padding the buffers, HotSort requires this
// explicit step to support use cases like:
//
//   - Dynamically allocating an output buffer
//   - Avoiding writing past the end of the input buffer
//

void
hotsort_vk_pad(struct hotsort_vk const * const hs,
               uint32_t const                  count,
               uint32_t * const                slabs_in,
               uint32_t * const                padded_in,
               uint32_t * const                padded_out);

//
// Append commands to the command buffer that, when enqueued, will:
//
//   1. Possibly pad the input buffer with max-valued keys
//   2. Load padded_in key-vals from the input buffer
//   3. Sort the key-vals
//   4. Store padded_out key-vals to the output buffer
//
// Pipeline barriers should be applied as necessary, both before and
// after invoking this function.
//
// Note that the algorithm *may* perform transfer operations before
// executing the first compute shader read.
//
// The algorithm ends with a compute shader write to a storage buffer.
//

void
hotsort_vk_sort(VkCommandBuffer                            cb,
                struct hotsort_vk const * const            hs,
                struct hotsort_vk_ds_offsets const * const offsets,
                uint32_t const                             count,
                uint32_t const                             padded_in,
                uint32_t const                             padded_out,
                bool const                                 linearize);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_HOTSORT_PLATFORMS_VK_HOTSORT_VK_H_
