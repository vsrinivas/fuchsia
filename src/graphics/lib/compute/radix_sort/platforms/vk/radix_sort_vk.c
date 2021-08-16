// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "radix_sort/platforms/vk/radix_sort_vk.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/macros.h"
#include "common/util.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "shaders/push.h"
#include "target_archive/target_archive.h"
#include "targets/radix_sort_vk_target.h"

//
//
//

#ifdef RADIX_SORT_VK_ENABLE_DEBUG_UTILS
#include "common/vk/debug_utils.h"
#endif

//
//
//

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
#include "radix_sort_vk_ext.h"
#endif

//
// NOTE: The library currently supports uint32_t and uint64_t keyvals.
//

#define RS_KV_DWORDS_MAX 2

//
//
//

struct rs_pipeline_layout_scatter
{
  VkPipelineLayout even;
  VkPipelineLayout odd;
};

struct rs_pipeline_scatter
{
  VkPipeline even;
  VkPipeline odd;
};

//
//
//

struct rs_pipeline_layouts_named
{
  VkPipelineLayout                  init;
  VkPipelineLayout                  fill;
  VkPipelineLayout                  histogram;
  VkPipelineLayout                  prefix;
  struct rs_pipeline_layout_scatter scatter[RS_KV_DWORDS_MAX];
};

struct rs_pipelines_named
{
  VkPipeline                 init;
  VkPipeline                 fill;
  VkPipeline                 histogram;
  VkPipeline                 prefix;
  struct rs_pipeline_scatter scatter[RS_KV_DWORDS_MAX];
};

// clang-format off
#define RS_PIPELINE_LAYOUTS_HANDLES (sizeof(struct rs_pipeline_layouts_named) / sizeof(VkPipelineLayout))
#define RS_PIPELINES_HANDLES        (sizeof(struct rs_pipelines_named)        / sizeof(VkPipeline))
// clang-format on

//
//
//

struct radix_sort_vk
{
  struct radix_sort_vk_target_config config;

  union
  {
    struct rs_pipeline_layouts_named named;
    VkPipelineLayout                 handles[RS_PIPELINE_LAYOUTS_HANDLES];
  } pipeline_layouts;

  union
  {
    struct rs_pipelines_named named;
    VkPipeline                handles[RS_PIPELINES_HANDLES];
  } pipelines;

  struct
  {
    struct
    {
      VkDeviceSize offset;
      VkDeviceSize range;
    } histograms;

    struct
    {
      VkDeviceSize offset;
    } partitions;

  } internal;
};

//
// FIXME(allanmac): memoize some of these calculations
//
void
radix_sort_vk_get_memory_requirements(struct radix_sort_vk const *               rs,
                                      uint32_t                                   count,
                                      struct radix_sort_vk_memory_requirements * mr)
{
  //
  // Keyval size
  //
  mr->keyval_size = rs->config.keyval_dwords * sizeof(uint32_t);

  //
  // Subgroup and workgroup sizes
  //
  uint32_t const histo_sg_size    = 1 << rs->config.histogram.subgroup_size_log2;
  uint32_t const histo_wg_size    = 1 << rs->config.histogram.workgroup_size_log2;
  uint32_t const prefix_sg_size   = 1 << rs->config.prefix.subgroup_size_log2;
  uint32_t const scatter_wg_size  = 1 << rs->config.scatter.workgroup_size_log2;
  uint32_t const internal_sg_size = MAX_MACRO(uint32_t, histo_sg_size, prefix_sg_size);

  //
  // If for some reason count is zero then initialize appropriately.
  //
  if (count == 0)
    {
      mr->keyvals_size       = 0;
      mr->keyvals_alignment  = mr->keyval_size * histo_sg_size;
      mr->internal_size      = 0;
      mr->internal_alignment = internal_sg_size * sizeof(uint32_t);
      mr->indirect_size      = 0;
      mr->indirect_alignment = internal_sg_size * sizeof(uint32_t);
    }
  else
    {
      //
      // Keyvals
      //

      // Round up to the scatter block size.
      //
      // Then round up to the histogram block size.
      //
      // Fill the difference between this new count and the original keyval
      // count.
      //
      // How many scatter blocks?
      //
      uint32_t const scatter_block_kvs = scatter_wg_size * rs->config.scatter.block_rows;
      uint32_t const scatter_blocks    = (count + scatter_block_kvs - 1) / scatter_block_kvs;
      uint32_t const count_ru_scatter  = scatter_blocks * scatter_block_kvs;

      //
      // How many histogram blocks?
      //
      // Note that it's OK to have more max-valued digits counted by the histogram
      // than sorted by the scatters because the sort is stable.
      //
      uint32_t const histo_block_kvs = histo_wg_size * rs->config.histogram.block_rows;
      uint32_t const histo_blocks    = (count_ru_scatter + histo_block_kvs - 1) / histo_block_kvs;
      uint32_t const count_ru_histo  = histo_blocks * histo_block_kvs;

      mr->keyvals_size      = mr->keyval_size * count_ru_histo;
      mr->keyvals_alignment = mr->keyval_size * histo_sg_size;

      //
      // Internal
      //
      // NOTE: Assumes .histograms are before .partitions.
      //
      // Last scatter workgroup skips writing to a partition.
      //
      // One histogram per (keyval byte + partitions)
      //
      uint32_t const partitions = scatter_blocks - 1;

      mr->internal_size      = (mr->keyval_size + partitions) * (RS_RADIX_SIZE * sizeof(uint32_t));
      mr->internal_alignment = internal_sg_size * sizeof(uint32_t);

      //
      // Indirect
      //
      mr->indirect_size      = sizeof(struct rs_indirect_info);
      mr->indirect_alignment = sizeof(struct u32vec4);
    }
}

//
//
//
#ifdef RADIX_SORT_VK_ENABLE_DEBUG_UTILS

static void
rs_debug_utils_set(VkDevice device, struct radix_sort_vk * rs)
{
  if (pfn_vkSetDebugUtilsObjectNameEXT != NULL)
    {
      VkDebugUtilsObjectNameInfoEXT name = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext      = NULL,
        .objectType = VK_OBJECT_TYPE_PIPELINE,
      };

      name.objectHandle = (uint64_t)rs->pipelines.named.init;
      name.pObjectName  = "radix_sort_init";
      vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));

      name.objectHandle = (uint64_t)rs->pipelines.named.fill;
      name.pObjectName  = "radix_sort_fill";
      vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));

      name.objectHandle = (uint64_t)rs->pipelines.named.histogram;
      name.pObjectName  = "radix_sort_histogram";
      vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));

      name.objectHandle = (uint64_t)rs->pipelines.named.prefix;
      name.pObjectName  = "radix_sort_prefix";
      vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));

      name.objectHandle = (uint64_t)rs->pipelines.named.scatter[0].even;
      name.pObjectName  = "radix_sort_scatter_0_even";
      vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));

      name.objectHandle = (uint64_t)rs->pipelines.named.scatter[0].odd;
      name.pObjectName  = "radix_sort_scatter_0_odd";
      vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));

      if (rs->config.keyval_dwords >= 2)
        {
          name.objectHandle = (uint64_t)rs->pipelines.named.scatter[1].even;
          name.pObjectName  = "radix_sort_scatter_1_even";
          vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));

          name.objectHandle = (uint64_t)rs->pipelines.named.scatter[1].odd;
          name.pObjectName  = "radix_sort_scatter_1_odd";
          vk_ok(pfn_vkSetDebugUtilsObjectNameEXT(device, &name));
        }
    }
}

#endif

//
//
//
struct radix_sort_vk_target
{
  struct target_archive_header ar_header;
};

//
// How many pipelines are there?
//
static uint32_t
rs_pipeline_count(struct radix_sort_vk const * rs)
{
  return 1 +                            // init
         1 +                            // fill
         1 +                            // histogram
         1 +                            // prefix
         2 * rs->config.keyval_dwords;  // scatters.even/odd[keyval_dwords]
}

//
//
//
struct radix_sort_vk *
radix_sort_vk_create(VkDevice                            device,
                     VkAllocationCallbacks const *       ac,
                     VkPipelineCache                     pc,
                     struct radix_sort_vk_target const * target)
{
  //
  // Must not be NULL
  //
  if (target == NULL)
    {
      return NULL;
    }

#ifndef RADIX_SORT_VK_DISABLE_VERIFY
  //
  // Verify target archive is valid archive
  //
  if (target->ar_header.magic != TARGET_ARCHIVE_MAGIC)
    {
#ifndef NDEBUG
      fprintf(stderr, "Error: Invalid target -- missing magic.");
#endif
      return NULL;
    }
#endif

  //
  // Get the Radix Sort target header.
  //
  struct target_archive_header const * const ar_header  = &target->ar_header;
  struct target_archive_entry const * const  ar_entries = ar_header->entries;
  uint32_t const * const                     ar_data    = ar_entries[ar_header->count - 1].data;

  union
  {
    struct radix_sort_vk_target_header const * header;
    uint32_t const *                           data;
  } const rs_target = { .data = ar_data };

  //
  // Verify target is compatible with the library.
  //
  // TODO(allanmac): Verify `ar_header->count` but note that not all target
  // archives will have a static count.
  //
#ifndef RADIX_SORT_VK_DISABLE_VERIFY
  if (rs_target.header->magic != RADIX_SORT_VK_HEADER_MAGIC)
    {
#ifndef NDEBUG
      fprintf(stderr, "Error: Target is not compatible with library.");
#endif
      return NULL;
    }
#endif

  //
  // Allocate radix_sort_vk
  //
  struct radix_sort_vk * const rs = malloc(sizeof(*rs));

  //
  // Save the config for layer
  //
  rs->config = rs_target.header->config;

  //
  // How many pipelines?
  //
  uint32_t const pipeline_count = rs_pipeline_count(rs);

  //
  // Prepare to create pipelines
  //
  VkPushConstantRange const pcr[] = {
    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_init) },

    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_fill) },

    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_histogram) },

    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_prefix) },

    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_scatter) },  // scatter_0_even

    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_scatter) },  // scatter_0_odd

    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_scatter) },  // scatter_1_even

    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,  //
      .offset     = 0,
      .size       = sizeof(struct rs_push_scatter) },  // scatter_1_odd
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

  for (uint32_t ii = 0; ii < pipeline_count; ii++)
    {
      plci.pPushConstantRanges = pcr + ii;

      vk(CreatePipelineLayout(device, &plci, NULL, rs->pipeline_layouts.handles + ii));
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

  VkShaderModule sms[ARRAY_LENGTH_MACRO(rs->pipelines.handles)];

  for (uint32_t ii = 0; ii < pipeline_count; ii++)
    {
      smci.codeSize = ar_entries[ii + 1].size;
      smci.pCode    = ar_data + (ar_entries[ii + 1].offset >> 2);

      vk(CreateShaderModule(device, &smci, ac, sms + ii));
    }

    //
    // If necessary, set the expected subgroup size
    //
#define RS_SUBGROUP_SIZE_CREATE_INFO_SET(size_)                                                    \
  {                                                                                                \
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,       \
    .pNext = NULL, .requiredSubgroupSize = size_,                                                  \
  }

#define RS_SUBGROUP_SIZE_CREATE_INFO_NAME(name_)                                                   \
  RS_SUBGROUP_SIZE_CREATE_INFO_SET(1 << rs_target.header->config.name_.subgroup_size_log2)

#define RS_SUBGROUP_SIZE_CREATE_INFO_ZERO(name_) RS_SUBGROUP_SIZE_CREATE_INFO_SET(0)

  VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT const rsscis[] = {
    RS_SUBGROUP_SIZE_CREATE_INFO_ZERO(init),       // init
    RS_SUBGROUP_SIZE_CREATE_INFO_ZERO(fill),       // fill
    RS_SUBGROUP_SIZE_CREATE_INFO_NAME(histogram),  // histogram
    RS_SUBGROUP_SIZE_CREATE_INFO_NAME(prefix),     // prefix
    RS_SUBGROUP_SIZE_CREATE_INFO_NAME(scatter),    // scatter[0].even
    RS_SUBGROUP_SIZE_CREATE_INFO_NAME(scatter),    // scatter[0].odd
    RS_SUBGROUP_SIZE_CREATE_INFO_NAME(scatter),    // scatter[1].even
    RS_SUBGROUP_SIZE_CREATE_INFO_NAME(scatter),    // scatter[1].odd
  };

  //
  // Define compute pipeline create infos
  //
#define RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(idx_)                                                 \
  { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,                                       \
    .pNext = NULL,                                                                                 \
    .flags = 0,                                                                                    \
    .stage = { .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,         \
               .pNext               = NULL,                                                        \
               .flags               = 0,                                                           \
               .stage               = VK_SHADER_STAGE_COMPUTE_BIT,                                 \
               .module              = sms[idx_],                                                   \
               .pName               = "main",                                                      \
               .pSpecializationInfo = NULL },                                                      \
                                                                                                   \
    .layout             = rs->pipeline_layouts.handles[idx_],                                      \
    .basePipelineHandle = VK_NULL_HANDLE,                                                          \
    .basePipelineIndex  = 0 }

  VkComputePipelineCreateInfo cpcis[] = {
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(0),  // init
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(1),  // fill
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(2),  // histogram
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(3),  // prefix
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(4),  // scatter[0].even
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(5),  // scatter[0].odd
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(6),  // scatter[1].even
    RS_COMPUTE_PIPELINE_CREATE_INFO_DECL(7),  // scatter[1].odd
  };

  //
  // Which of these compute pipelines require subgroup size control?
  //
  if (rs_target.header->extensions.named.EXT_subgroup_size_control)
    {
      for (uint32_t ii = 0; ii < pipeline_count; ii++)
        {
          if (rsscis[ii].requiredSubgroupSize > 1)
            {
              // clang-format off
              cpcis[ii].stage.pNext = rsscis + ii;
              cpcis[ii].stage.flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;
              // clang-format on
            }
        }
    }

  //
  // Create the compute pipelines
  //
  vk(CreateComputePipelines(device, pc, pipeline_count, cpcis, ac, rs->pipelines.handles));

  //
  // Shader modules can be destroyed now
  //
  for (uint32_t ii = 0; ii < pipeline_count; ii++)
    {
      vkDestroyShaderModule(device, sms[ii], ac);
    }

#ifdef RADIX_SORT_VK_ENABLE_DEBUG_UTILS
  //
  // Tag pipelines with names
  //
  rs_debug_utils_set(device, rs);
#endif

  //
  // Calculate "internal" buffer offsets
  //
  size_t const keyval_bytes = rs->config.keyval_dwords * sizeof(uint32_t);

  // the .range calculation assumes an 8-bit radix
  rs->internal.histograms.offset = 0;
  rs->internal.histograms.range  = keyval_bytes * (RS_RADIX_SIZE * sizeof(uint32_t));

  //
  // NOTE(allanmac): The partitions.offset must be aligned differently if
  // RS_RADIX_LOG2 is less than the target's subgroup size log2.  At this time,
  // no GPU that meets this criteria.
  //
  rs->internal.partitions.offset = rs->internal.histograms.offset + rs->internal.histograms.range;

  return rs;
}

//
//
//
void
radix_sort_vk_destroy(struct radix_sort_vk *              rs,
                      VkDevice                            device,
                      VkAllocationCallbacks const * const ac)
{
  uint32_t const pipeline_count = rs_pipeline_count(rs);

  // destroy pipelines
  for (uint32_t ii = 0; ii < pipeline_count; ii++)
    {
      vkDestroyPipeline(device, rs->pipelines.handles[ii], ac);
    }

  // destroy pipeline layouts
  for (uint32_t ii = 0; ii < pipeline_count; ii++)
    {
      vkDestroyPipelineLayout(device, rs->pipeline_layouts.handles[ii], ac);
    }

  free(rs);
}

//
//
//
static VkDeviceAddress
rs_get_devaddr(VkDevice device, VkDescriptorBufferInfo const * dbi)
{
  VkBufferDeviceAddressInfo const bdai = {

    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    .pNext  = NULL,
    .buffer = dbi->buffer
  };

  VkDeviceAddress const devaddr = vkGetBufferDeviceAddress(device, &bdai) + dbi->offset;

  return devaddr;
}

//
//
//
#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS

void
rs_ext_cmd_write_timestamp(struct radix_sort_vk_ext_timestamps * ext_timestamps,
                           VkCommandBuffer                       cb,
                           VkPipelineStageFlagBits               pipeline_stage)
{
  if ((ext_timestamps != NULL) &&
      (ext_timestamps->timestamps_set < ext_timestamps->timestamp_count))
    {
      vkCmdWriteTimestamp(cb,
                          pipeline_stage,
                          ext_timestamps->timestamps,
                          ext_timestamps->timestamps_set++);
    }
}

#endif

//
//
//

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS

struct radix_sort_vk_ext_base
{
  void *                      ext;
  enum radix_sort_vk_ext_type type;
};

#endif

//
//
//
void
radix_sort_vk_sort(struct radix_sort_vk const *           rs,
                   struct radix_sort_vk_sort_info const * info,
                   VkDevice                               device,
                   VkCommandBuffer                        cb,
                   VkDescriptorBufferInfo const **        keyvals_sorted)
{
  //
  // Anything to do?
  //
  if ((info->count <= 1) || (info->key_bits == 0))
    {
      *keyvals_sorted = info->keyvals_even;
      return;
    }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  //
  // Any extensions?
  //
  struct radix_sort_vk_ext_timestamps * ext_timestamps = NULL;

  void * ext_next = info->ext;

  while (ext_next != NULL)
    {
      struct radix_sort_vk_ext_base * const base = ext_next;

      switch (base->type)
        {
          case RADIX_SORT_VK_EXT_TIMESTAMPS:
            ext_timestamps                 = ext_next;
            ext_timestamps->timestamps_set = 0;
            break;
        }

      ext_next = base->ext;
    }
#endif

    ////////////////////////////////////////////////////////////////////////
    //
    // OVERVIEW
    //
    //   1. Pad the keyvals in `scatter_even`.
    //   2. Zero the `histograms` and `partitions`.
    //      --- BARRIER ---
    //   3. HISTOGRAM is dispatched before PREFIX.
    //      --- BARRIER ---
    //   4. PREFIX is dispatched before the first SCATTER.
    //      --- BARRIER ---
    //   5. One or more SCATTER dispatches.
    //
    // Note that the `partitions` buffer can be zeroed anytime before the first
    // scatter.
    //
    ////////////////////////////////////////////////////////////////////////

    //
    // Label the command buffer
    //
#ifdef RADIX_SORT_VK_ENABLE_DEBUG_UTILS
  if (pfn_vkCmdBeginDebugUtilsLabelEXT != NULL)
    {
      VkDebugUtilsLabelEXT const label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = NULL,
        .pLabelName = "radix_sort_vk_sort",
      };

      pfn_vkCmdBeginDebugUtilsLabelEXT(cb, &label);
    }
#endif

  //
  // How many passes?
  //
  uint32_t const keyval_bytes = rs->config.keyval_dwords * (uint32_t)sizeof(uint32_t);
  uint32_t const keyval_bits  = keyval_bytes * 8;
  uint32_t const key_bits     = MIN_MACRO(uint32_t, info->key_bits, keyval_bits);
  uint32_t const passes       = (key_bits + RS_RADIX_LOG2 - 1) / RS_RADIX_LOG2;

  *keyvals_sorted = ((passes & 1) != 0) ? info->keyvals_odd : info->keyvals_even;

  ////////////////////////////////////////////////////////////////////////
  //
  // PAD KEYVALS AND ZERO HISTOGRAM/PARTITIONS
  //
  // Pad fractional blocks with max-valued keyvals.
  //
  // Zero the histograms and partitions buffer.
  //
  // This assumes the partitions follow the histograms.
  //
#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
#endif

  //
  // FIXME(allanmac): Consider precomputing some of these values and hang them
  // off `rs`.
  //

  //
  // How many scatter blocks?
  //
  uint32_t const scatter_wg_size   = 1 << rs->config.scatter.workgroup_size_log2;
  uint32_t const scatter_block_kvs = scatter_wg_size * rs->config.scatter.block_rows;
  uint32_t const scatter_blocks    = (info->count + scatter_block_kvs - 1) / scatter_block_kvs;
  uint32_t const count_ru_scatter  = scatter_blocks * scatter_block_kvs;

  //
  // How many histogram blocks?
  //
  // Note that it's OK to have more max-valued digits counted by the histogram
  // than sorted by the scatters because the sort is stable.
  //
  uint32_t const histo_wg_size   = 1 << rs->config.histogram.workgroup_size_log2;
  uint32_t const histo_block_kvs = histo_wg_size * rs->config.histogram.block_rows;
  uint32_t const histo_blocks    = (count_ru_scatter + histo_block_kvs - 1) / histo_block_kvs;
  uint32_t const count_ru_histo  = histo_blocks * histo_block_kvs;

  //
  // Fill with max values
  //
  if (count_ru_histo > info->count)
    {
      vkCmdFillBuffer(cb,  //
                      info->keyvals_even->buffer,
                      info->keyvals_even->offset + info->count * keyval_bytes,
                      (count_ru_histo - info->count) * keyval_bytes,
                      0xFFFFFFFF);
    }

  //
  // Zero histograms and invalidate partitions.
  //
  // Note that the partition invalidation only needs to be performed once
  // because the even/odd scatter dispatches rely on the the previous pass to
  // leave the partitions in an invalid state.
  //
  // Note that the last workgroup doesn't read/write a partition so it doesn't
  // need to be initialized.
  //
  uint32_t const histo_partition_count = passes + scatter_blocks - 1;
  uint32_t       pass_idx              = (keyval_bytes - passes);

  VkDeviceSize const fill_base = pass_idx * (RS_RADIX_SIZE * sizeof(uint32_t));

  vkCmdFillBuffer(cb,
                  info->internal->buffer,
                  info->internal->offset + rs->internal.histograms.offset + fill_base,
                  histo_partition_count * (RS_RADIX_SIZE * sizeof(uint32_t)),
                  0);

  ////////////////////////////////////////////////////////////////////////
  //
  // Pipeline: HISTOGRAM
  //
  // TODO(allanmac): All subgroups should try to process approximately the same
  // number of blocks in order to minimize tail effects.  This was implemented
  // and reverted but should be reimplemented and benchmarked later.
  //
#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_TRANSFER_BIT);
#endif

  vk_barrier_transfer_w_to_compute_r(cb);

  // clang-format off
  VkDeviceAddress const devaddr_histograms   = rs_get_devaddr(device, info->internal) + rs->internal.histograms.offset;
  VkDeviceAddress const devaddr_keyvals_even = rs_get_devaddr(device, info->keyvals_even);
  // clang-format on

  //
  // Dispatch histogram
  //
  struct rs_push_histogram const push_histogram = {

    .devaddr_histograms = devaddr_histograms,
    .devaddr_keyvals    = devaddr_keyvals_even,
    .passes             = passes
  };

  vkCmdPushConstants(cb,
                     rs->pipeline_layouts.named.histogram,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_histogram),
                     &push_histogram);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rs->pipelines.named.histogram);

  vkCmdDispatch(cb, histo_blocks, 1, 1);

  ////////////////////////////////////////////////////////////////////////
  //
  // Pipeline: PREFIX
  //
  // Launch one workgroup per pass.
  //
#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  vk_barrier_compute_w_to_compute_r(cb);

  struct rs_push_prefix const push_prefix = {

    .devaddr_histograms = devaddr_histograms,
  };

  vkCmdPushConstants(cb,
                     rs->pipeline_layouts.named.prefix,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(push_prefix),
                     &push_prefix);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rs->pipelines.named.prefix);

  vkCmdDispatch(cb, passes, 1, 1);

  ////////////////////////////////////////////////////////////////////////
  //
  // Pipeline: SCATTER
  //
#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  vk_barrier_compute_w_to_compute_r(cb);

  // clang-format off
  uint32_t        const histogram_offset    = pass_idx * (RS_RADIX_SIZE * sizeof(uint32_t));
  VkDeviceAddress const devaddr_keyvals_odd = rs_get_devaddr(device, info->keyvals_odd);
  VkDeviceAddress const devaddr_partitions  = rs_get_devaddr(device, info->internal) + rs->internal.partitions.offset;
  // clang-format on

  struct rs_push_scatter push_scatter = {

    .devaddr_keyvals_even = devaddr_keyvals_even,
    .devaddr_keyvals_odd  = devaddr_keyvals_odd,
    .devaddr_partitions   = devaddr_partitions,
    .devaddr_histograms   = devaddr_histograms + histogram_offset,
    .pass_offset          = (pass_idx & 3) * RS_RADIX_LOG2,
  };

  {
    uint32_t const pass_dword = pass_idx / 4;

    vkCmdPushConstants(cb,
                       rs->pipeline_layouts.named.scatter[pass_dword].even,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(push_scatter),
                       &push_scatter);

    vkCmdBindPipeline(cb,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      rs->pipelines.named.scatter[pass_dword].even);
  }

  bool is_even = true;

  while (true)
    {
      vkCmdDispatch(cb, scatter_blocks, 1, 1);

      //
      // Continue?
      //
      if (++pass_idx >= keyval_bytes)
        break;

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
      rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif
      vk_barrier_compute_w_to_compute_r(cb);

      // clang-format off
      is_even                         ^= true;
      push_scatter.devaddr_histograms += (RS_RADIX_SIZE * sizeof(uint32_t));
      push_scatter.pass_offset         = (pass_idx & 3) * RS_RADIX_LOG2;
      // clang-format on

      uint32_t const pass_dword = pass_idx / 4;

      //
      // Update push constants that changed
      //
      VkPipelineLayout const pl = is_even ? rs->pipeline_layouts.named.scatter[pass_dword].even  //
                                          : rs->pipeline_layouts.named.scatter[pass_dword].odd;
      vkCmdPushConstants(cb,
                         pl,
                         VK_SHADER_STAGE_COMPUTE_BIT,
                         OFFSETOF_MACRO(struct rs_push_scatter, devaddr_histograms),
                         sizeof(push_scatter.devaddr_histograms) + sizeof(push_scatter.pass_offset),
                         &push_scatter.devaddr_histograms);

      //
      // Bind new pipeline
      //
      VkPipeline const p = is_even ? rs->pipelines.named.scatter[pass_dword].even  //
                                   : rs->pipelines.named.scatter[pass_dword].odd;

      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  //
  // End the label
  //
#ifdef RADIX_SORT_VK_ENABLE_DEBUG_UTILS
  if (pfn_vkCmdEndDebugUtilsLabelEXT != NULL)
    {
      pfn_vkCmdEndDebugUtilsLabelEXT(cb);
    }
#endif
}

//
//
//
void
radix_sort_vk_sort_indirect(struct radix_sort_vk const *                    rs,
                            struct radix_sort_vk_sort_indirect_info const * info,
                            VkDevice                                        device,
                            VkCommandBuffer                                 cb,
                            VkDescriptorBufferInfo const **                 keyvals_sorted)
{
  //
  // Anything to do?
  //
  if (info->key_bits == 0)
    {
      *keyvals_sorted = info->keyvals_even;
      return;
    }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  //
  // Any extensions?
  //
  struct radix_sort_vk_ext_timestamps * ext_timestamps = NULL;

  void * ext_next = info->ext;

  while (ext_next != NULL)
    {
      struct radix_sort_vk_ext_base * const base = ext_next;

      switch (base->type)
        {
          case RADIX_SORT_VK_EXT_TIMESTAMPS:
            ext_timestamps                 = ext_next;
            ext_timestamps->timestamps_set = 0;
            break;
        }

      ext_next = base->ext;
    }
#endif

    ////////////////////////////////////////////////////////////////////////
    //
    // OVERVIEW
    //
    //   1. Init
    //      --- BARRIER ---
    //   2. Pad the keyvals in `scatter_even`.
    //   3. Zero the `histograms` and `partitions`.
    //      --- BARRIER ---
    //   4. HISTOGRAM is dispatched before PREFIX.
    //      --- BARRIER ---
    //   5. PREFIX is dispatched before the first SCATTER.
    //      --- BARRIER ---
    //   6. One or more SCATTER dispatches.
    //
    // Note that the `partitions` buffer can be zeroed anytime before the first
    // scatter.
    //
    ////////////////////////////////////////////////////////////////////////

    //
    // Label the command buffer
    //
#ifdef RADIX_SORT_VK_ENABLE_DEBUG_UTILS
  if (pfn_vkCmdBeginDebugUtilsLabelEXT != NULL)
    {
      VkDebugUtilsLabelEXT const label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = NULL,
        .pLabelName = "radix_sort_vk_sort_indirect",
      };

      pfn_vkCmdBeginDebugUtilsLabelEXT(cb, &label);
    }
#endif

  //
  // How many passes?
  //
  uint32_t const keyval_bytes = rs->config.keyval_dwords * (uint32_t)sizeof(uint32_t);
  uint32_t const keyval_bits  = keyval_bytes * 8;
  uint32_t const key_bits     = MIN_MACRO(uint32_t, info->key_bits, keyval_bits);
  uint32_t const passes       = (key_bits + RS_RADIX_LOG2 - 1) / RS_RADIX_LOG2;
  uint32_t       pass_idx     = (keyval_bytes - passes);

  *keyvals_sorted = ((passes & 1) != 0) ? info->keyvals_odd : info->keyvals_even;

  //
  //
  //
  // clang-format off
  VkDeviceAddress const devaddr_info         = rs_get_devaddr(device, info->indirect);
  VkDeviceAddress const devaddr_count        = rs_get_devaddr(device, info->count);
  VkDeviceAddress const devaddr_histograms   = rs_get_devaddr(device, info->internal) + rs->internal.histograms.offset;
  VkDeviceAddress const devaddr_keyvals_even = rs_get_devaddr(device, info->keyvals_even);
  // clang-format on

  //
  // START
  //
#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
#endif

  //
  // INIT
  //
  {
    struct rs_push_init const push_init = {

      .devaddr_info  = devaddr_info,
      .devaddr_count = devaddr_count,
      .passes        = passes
    };

    vkCmdPushConstants(cb,
                       rs->pipeline_layouts.named.init,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(push_init),
                       &push_init);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rs->pipelines.named.init);

    vkCmdDispatch(cb, 1, 1, 1);
  }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  vk_barrier_compute_w_to_indirect_compute_r(cb);

  {
    //
    // PAD
    //
    struct rs_push_fill const push_pad = {

      .devaddr_info   = devaddr_info + offsetof(struct rs_indirect_info, pad),
      .devaddr_dwords = devaddr_keyvals_even,
      .dword          = 0xFFFFFFFF
    };

    vkCmdPushConstants(cb,
                       rs->pipeline_layouts.named.fill,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(push_pad),
                       &push_pad);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rs->pipelines.named.fill);

    vkCmdDispatchIndirect(cb,
                          info->indirect->buffer,
                          offsetof(struct rs_indirect_info, dispatch.pad));
  }

  //
  // ZERO
  //
  {
    VkDeviceSize const histo_offset = pass_idx * (sizeof(uint32_t) * RS_RADIX_SIZE);

    struct rs_push_fill const push_zero = {

      .devaddr_info   = devaddr_info + offsetof(struct rs_indirect_info, zero),
      .devaddr_dwords = devaddr_histograms + histo_offset,
      .dword          = 0
    };

    vkCmdPushConstants(cb,
                       rs->pipeline_layouts.named.fill,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(push_zero),
                       &push_zero);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rs->pipelines.named.fill);

    vkCmdDispatchIndirect(cb,
                          info->indirect->buffer,
                          offsetof(struct rs_indirect_info, dispatch.zero));
  }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  vk_barrier_compute_w_to_compute_r(cb);

  //
  // HISTOGRAM
  //
  {
    struct rs_push_histogram const push_histogram = {

      .devaddr_histograms = devaddr_histograms,
      .devaddr_keyvals    = devaddr_keyvals_even,
      .passes             = passes
    };

    vkCmdPushConstants(cb,
                       rs->pipeline_layouts.named.histogram,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(push_histogram),
                       &push_histogram);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rs->pipelines.named.histogram);

    vkCmdDispatchIndirect(cb,
                          info->indirect->buffer,
                          offsetof(struct rs_indirect_info, dispatch.histogram));
  }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  vk_barrier_compute_w_to_compute_r(cb);

  //
  // PREFIX
  //
  {
    struct rs_push_prefix const push_prefix = {

      .devaddr_histograms = devaddr_histograms,
    };

    vkCmdPushConstants(cb,
                       rs->pipeline_layouts.named.prefix,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(push_prefix),
                       &push_prefix);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rs->pipelines.named.prefix);

    vkCmdDispatch(cb, passes, 1, 1);
  }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  vk_barrier_compute_w_to_compute_r(cb);

  //
  // SCATTER
  //
  {
    // clang-format off
    uint32_t        const histogram_offset    = pass_idx * (RS_RADIX_SIZE * sizeof(uint32_t));
    VkDeviceAddress const devaddr_keyvals_odd = rs_get_devaddr(device, info->keyvals_odd);
    VkDeviceAddress const devaddr_partitions  = rs_get_devaddr(device, info->internal) + rs->internal.partitions.offset;
    // clang-format on

    struct rs_push_scatter push_scatter = {

      .devaddr_keyvals_even = devaddr_keyvals_even,
      .devaddr_keyvals_odd  = devaddr_keyvals_odd,
      .devaddr_partitions   = devaddr_partitions,
      .devaddr_histograms   = devaddr_histograms + histogram_offset,
      .pass_offset          = (pass_idx & 3) * RS_RADIX_LOG2,
    };

    {
      uint32_t const pass_dword = pass_idx / 4;

      vkCmdPushConstants(cb,
                         rs->pipeline_layouts.named.scatter[pass_dword].even,
                         VK_SHADER_STAGE_COMPUTE_BIT,
                         0,
                         sizeof(push_scatter),
                         &push_scatter);

      vkCmdBindPipeline(cb,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        rs->pipelines.named.scatter[pass_dword].even);
    }

    bool is_even = true;

    while (true)
      {
        vkCmdDispatchIndirect(cb,
                              info->indirect->buffer,
                              offsetof(struct rs_indirect_info, dispatch.scatter));

        //
        // Continue?
        //
        if (++pass_idx >= keyval_bytes)
          break;

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
        rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

        vk_barrier_compute_w_to_compute_r(cb);

        // clang-format off
        is_even                         ^= true;
        push_scatter.devaddr_histograms += (RS_RADIX_SIZE * sizeof(uint32_t));
        push_scatter.pass_offset         = (pass_idx & 3) * RS_RADIX_LOG2;
        // clang-format on

        uint32_t const pass_dword = pass_idx / 4;

        //
        // Update push constants that changed
        //
        VkPipelineLayout const pl = is_even
                                      ? rs->pipeline_layouts.named.scatter[pass_dword].even  //
                                      : rs->pipeline_layouts.named.scatter[pass_dword].odd;
        vkCmdPushConstants(
          cb,
          pl,
          VK_SHADER_STAGE_COMPUTE_BIT,
          OFFSETOF_MACRO(struct rs_push_scatter, devaddr_histograms),
          sizeof(push_scatter.devaddr_histograms) + sizeof(push_scatter.pass_offset),
          &push_scatter.devaddr_histograms);

        //
        // Bind new pipeline
        //
        VkPipeline const p = is_even ? rs->pipelines.named.scatter[pass_dword].even  //
                                     : rs->pipelines.named.scatter[pass_dword].odd;

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p);
      }
  }

#ifdef RADIX_SORT_VK_ENABLE_EXTENSIONS
  rs_ext_cmd_write_timestamp(ext_timestamps, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
#endif

  //
  // End the label
  //
#ifdef RADIX_SORT_VK_ENABLE_DEBUG_UTILS
  if (pfn_vkCmdEndDebugUtilsLabelEXT != NULL)
    {
      pfn_vkCmdEndDebugUtilsLabelEXT(cb);
    }
#endif
}

//
//
//
