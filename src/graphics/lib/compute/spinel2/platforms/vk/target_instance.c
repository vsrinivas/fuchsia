// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Load the Spinel target and creating pipelines and pipeline layouts.
//

#include "target_instance.h"

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

#include "common/macros.h"
#include "common/util.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "shaders/push.h"
#include "target.h"
#include "target_archive/target_archive.h"

//
// Are Vulkan objects being tagged with names?
//
#ifdef SPN_VK_ENABLE_DEBUG_UTILS
#include "common/vk/debug_utils.h"
#endif

//
// Validate SPN_P_COUNT
//
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_) +1

#if (SPN_P_COUNT != (0 + SPN_P_EXPAND()))
#error "(SPN_P_COUNT != (0 + SPN_P_EXPAND()))"
#endif

//
//
//
static void
spinel_target_instance_destroy_spinel(struct spinel_target_instance *     ti,
                                      VkDevice                            d,
                                      VkAllocationCallbacks const * const ac)
{
  //
  // destroy pipelines
  //
  for (uint32_t ii = 0; ii < SPN_P_COUNT; ii++)
    {
      vkDestroyPipeline(d, ti->pipelines.handles[ii], ac);
    }

  //
  // destroy pipeline layouts
  //
  for (uint32_t ii = 0; ii < SPN_P_COUNT; ii++)
    {
      vkDestroyPipelineLayout(d, ti->pipeline_layouts.handles[ii], ac);
    }
}

//
//
//
void
spinel_target_instance_destroy(struct spinel_target_instance *     ti,
                               VkDevice                            d,
                               VkAllocationCallbacks const * const ac)
{
  radix_sort_vk_destroy(ti->rs, d, ac);

  spinel_target_instance_destroy_spinel(ti, d, ac);
}

//
//
//
#ifdef SPN_VK_ENABLE_DEBUG_UTILS

static void
spinel_debug_utils_set(VkDevice device, struct spinel_target_instance * ti)
{
  if (pfn_vkSetDebugUtilsObjectNameEXT != NULL)
    {
      VkDebugUtilsObjectNameInfoEXT duoni = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext      = NULL,
        .objectType = VK_OBJECT_TYPE_PIPELINE,
      };

#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_)                                                           \
  duoni.objectHandle = (uint64_t)ti->pipelines.named.pipeline_;                                    \
  duoni.pObjectName  = #pipeline_;                                                                 \
  vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &duoni));

      SPN_P_EXPAND()
    }
}

#endif

//
// We only need to define the spinel_vk_target layout here.
//
struct spinel_vk_target
{
  struct target_archive_header ar_header;
};

//
//
//
bool
spinel_target_instance_create(struct spinel_target_instance * ti,
                              VkDevice                        d,
                              VkAllocationCallbacks const *   ac,
                              VkPipelineCache                 pc,
                              struct spinel_vk_target const * target)
{
  //
  // Must not be NULL
  //
  if (target == NULL)
    {
      return false;
    }

#ifndef SPN_VK_DISABLE_VERIFY
  //
  // Verify target archive is valid archive
  //
  if (target->ar_header.magic != TARGET_ARCHIVE_MAGIC)
    {
#ifndef NDEBUG
      fprintf(stderr, "Error: Invalid target -- missing magic.");
#endif
      return false;
    }
#endif

  //
  // Get the spinel_vk_target header
  //
  struct target_archive_header const * const ar_header  = &target->ar_header;
  struct target_archive_entry const * const  ar_entries = ar_header->entries;
  uint32_t const * const                     ar_data    = ar_entries[ar_header->count - 1].data;

  union
  {
    struct spinel_target_header const * header;
    uint32_t const *                    data;
  } const spinel_header_data = { .data = ar_data };

#ifndef SPN_VK_DISABLE_VERIFY
  //
  // Verify target is compatible with the library
  //
  if (spinel_header_data.header->magic != SPN_HEADER_MAGIC)
    {
#ifndef NDEBUG
      fprintf(stderr, "Error: Target is not compatible with library.");
#endif
      return false;
    }
#endif

  //
  // Save the config for layer
  //
  ti->config = spinel_header_data.header->config;

  //
  // Prepare to create pipelines
  //
  VkPushConstantRange const pcr[] = {
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_)                                                           \
  { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(SPN_PUSH_TYPE(push_)) },

    SPN_P_EXPAND()
  };

  VkPipelineLayoutCreateInfo plci = {

    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .setLayoutCount         = 0,
    .pSetLayouts            = NULL,
    .pushConstantRangeCount = 1,
    // .pPushConstantRanges = pcr + ii;
  };

  for (uint32_t ii = 0; ii < SPN_P_COUNT; ii++)
    {
      plci.pPushConstantRanges = pcr + ii;

      vk(CreatePipelineLayout(d, &plci, ac, ti->pipeline_layouts.handles + ii));
    }

  //
  // Create compute pipelines
  //
  VkShaderModuleCreateInfo smci = {

    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    // .codeSize = ar_entries[...].size;
    // .pCode    = ar_data + ...;
  };

  union
  {
    struct
    {
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_) VkShaderModule pipeline_;

      SPN_P_EXPAND()

    } named;

    VkShaderModule handles[SPN_P_COUNT];
  } sms;

  for (uint32_t ii = 0; ii < SPN_P_COUNT; ii++)
    {
      smci.codeSize = ar_entries[ii + 1].size;
      smci.pCode    = ar_data + (ar_entries[ii + 1].offset >> 2);

      vk(CreateShaderModule(d, &smci, ac, sms.handles + ii));
    }

    //
    // If necessary, set the expected subgroup size
    //
#define SPN_SUBGROUP_SIZE_CREATE_INFO_SET(size_)                                                   \
  {                                                                                                \
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,       \
    .pNext = NULL,                                                                                 \
    .requiredSubgroupSize = size_,                                                                 \
  }

#define SPN_SUBGROUP_SIZE_CREATE_INFO_NAME(name_)                                                  \
  SPN_SUBGROUP_SIZE_CREATE_INFO_SET(                                                               \
    1 << spinel_header_data.header->config.group_sizes.named.name_.subgroup_log2)

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT const rsscis[] = {
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_) SPN_SUBGROUP_SIZE_CREATE_INFO_NAME(pipeline_),

    SPN_P_EXPAND()
  };

  //
  // Define compute pipeline create infos
  //
#define SPN_COMPUTE_PIPELINE_CREATE_INFO_DECL(name_)                                               \
  { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,                                       \
    .pNext = NULL,                                                                                 \
    .flags = 0,                                                                                    \
    .stage = { .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,         \
               .pNext               = NULL,                                                        \
               .flags               = 0,                                                           \
               .stage               = VK_SHADER_STAGE_COMPUTE_BIT,                                 \
               .module              = sms.named.name_,                                             \
               .pName               = "main",                                                      \
               .pSpecializationInfo = NULL },                                                      \
                                                                                                   \
    .layout             = ti->pipeline_layouts.named.name_,                                        \
    .basePipelineHandle = VK_NULL_HANDLE,                                                          \
    .basePipelineIndex  = 0 }

  VkComputePipelineCreateInfo cpcis[] = {
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_) SPN_COMPUTE_PIPELINE_CREATE_INFO_DECL(pipeline_),

    SPN_P_EXPAND()
  };

  //
  // Which of these compute pipelines require subgroup size control?
  //
  if (spinel_header_data.header->extensions.named.EXT_subgroup_size_control)
    {
      for (uint32_t ii = 0; ii < SPN_P_COUNT; ii++)
        {
          if (rsscis[ii].requiredSubgroupSize > 1)
            {
              cpcis[ii].stage.pNext = rsscis + ii;
            }
        }
    }

  //
  // Create the compute pipelines
  //
  vk(CreateComputePipelines(d, pc, SPN_P_COUNT, cpcis, ac, ti->pipelines.handles));

  //
  // Shader modules can be destroyed now
  //
  for (uint32_t ii = 0; ii < SPN_P_COUNT; ii++)
    {
      vkDestroyShaderModule(d, sms.handles[ii], ac);
    }

#ifdef SPN_VK_ENABLE_DEBUG_UTILS
  //
  // Tag pipelines with names
  //
  spinel_debug_utils_set(d, ti);
#endif

  //
  // Get the embedded radix_vk_target
  //
  union
  {
    struct radix_sort_vk_target const * target;
    uint32_t const *                    data;
  } const radix_target_data = { .data = ar_data + (ar_entries[ar_header->count - 1].offset >> 2) };

  //
  // Create radix sort instance
  //
  ti->rs = radix_sort_vk_create(d, ac, pc, radix_target_data.target);

  if (ti->rs == NULL)
    {
      spinel_target_instance_destroy_spinel(ti, d, ac);

      return false;
    }

  return true;
}

//
//
//
