// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
// NOTE: reevaluate the HotSort/VK API once "Physical Storage Buffer
// Access" is more widely supported.
//

#include <vulkan/vulkan_core.h>

//
//
//

#include <stdint.h>
#include <stdbool.h>

//
//
//

#include "hs_vk_target.h"

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
// The buffer offsets can vary with each invocation of hs_vk_sort().
//

//
// HotSort push constants are expected at offset 0
//

struct hs_vk_push
{
  uint32_t kv_offset_in;
  uint32_t kv_offset_out;
  uint32_t kv_count;
};

#define HS_VK_PUSH_CONSTANT_RANGE_STAGE_FLAGS VK_SHADER_STAGE_COMPUTE_BIT
#define HS_VK_PUSH_CONSTANT_RANGE_OFFSET      0
#define HS_VK_PUSH_CONSTANT_RANGE_SIZE        sizeof(struct hs_vk_push)

//
// Declare the descriptor set layout binding locations for the
// input and output key-value storage buffers.
//
// If the input and output locations are identical then the sort will
// be performed "in place".
//

struct hs_vk_ds_locations
{
  struct {
    uint32_t set;
    uint32_t binding;
  } in;
  struct {
    uint32_t set;
    uint32_t binding;
  } out;
};

//
// Declare the offsets of the key-value arrays before sorting.
//

struct hs_vk_ds_offsets
{
  VkDeviceSize in;
  VkDeviceSize out;
};

//
// Create a HotSort instance for a target that operates on storage
// buffers at specific descriptor set locations.
//

struct hs_vk *
hs_vk_create(VkDevice                                device,
             VkAllocationCallbacks     const *       allocator,
             VkPipelineCache                         pipeline_cache,
             VkPipelineLayout                        pipeline_layout,
             struct hs_vk_ds_locations const * const locations,
             struct hs_vk_target       const * const target);

//
// Resources will be disposed of with the same device and allocator
// used for creation.
//

void
hs_vk_release(VkDevice                            device,
              VkAllocationCallbacks const * const allocator,
              struct hs_vk                * const hs);

//
// Explicitly reveal what padding of maximum valued key-vals will be
// applied to the input and output buffers.
//
//   Input:
//     count      : input number of key-vals
//
//   Output:
//     padded_in  : adjusted number of input key-vals
//     padded_out : adjusted number of output key-vals
//
// Instead of implicitly padding the buffers, HotSort requires this
// explicit step to support use cases like:
//
//   - writing past the end of the input buffer
//   - dynamically allocating an output buffer
//

void
hs_vk_pad(struct hs_vk const * const hs,
          uint32_t             const count,
          uint32_t           * const padded_in,
          uint32_t           * const padded_out);

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
hs_vk_sort(VkCommandBuffer                       cb,
           struct hs_vk            const * const hs,
           struct hs_vk_ds_offsets const * const offsets,
           uint32_t                        const count,
           uint32_t                        const padded_in,
           uint32_t                        const padded_out,
           bool                            const linearize);

//
//
//
