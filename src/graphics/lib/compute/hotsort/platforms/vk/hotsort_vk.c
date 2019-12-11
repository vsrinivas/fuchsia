// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hotsort_vk.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/util.h"
#include "common/vk/assert.h"
#include "common/vk/barrier.h"
#include "targets/hotsort_vk_target.h"

//
//
//

#ifndef NDEBUG

#if defined(HOTSORT_VK_SHADER_INFO_AMD_STATISTICS) ||                                              \
  defined(HOTSORT_VK_SHADER_INFO_AMD_DISASSEMBLY)
#include "common/vk/shader_info_amd.h"
#endif

#endif

//
//
//

#ifndef NDEBUG
#include <stdio.h>
#endif

//
// We want concurrent kernel execution to occur in a few places.
//
// The summary is:
//
//   1) If necessary, some max valued keys are written to the end of
//      the in/out buffers.
//
//   2) Blocks of slabs of keys are sorted.
//
//   3) If necesary, the blocks of slabs are merged until complete.
//
//   4) If requested, the slabs will be converted from slab ordering
//      to linear ordering.
//
// Below is the general "happens-before" relationship between HotSort
// compute kernels.
//
// Note the diagram assumes different input and output buffers.  If
// they're the same, then the first merge doesn't include the pad_in
// event in the wait list.
//
//                    +--------+              +---------+
//                    | pad_in |              | pad_out |
//                    +----+---+              +----+----+
//                         |                       |
//                         |                WAITFOR(pad_in)
//                         |                       |
//                         |                 +-----v-----+
//                         |                 |           |
//                         |            +----v----+ +----v----+
//                         |            | bs_full | | bs_frac |
//                         |            +----+----+ +----+----+
//                         |                 |           |
//                         |                 +-----v-----+
//                         |                       |
//                         |  +------NO------JUST ONE BLOCK?
//                         | /                     |
//                         |/                     YES
//                         +                       |
//                         |                       v
//                         |            END_WITH(bs_full,bs_frac)
//                         |
//                         |
//        WAITFOR(pad_out,bs_full,bs_frac) >>> first iteration of loop <<<
//                         |
//                         |
//                         +-----------<------------+
//                         |                        |
//                   +-----v-----+                  |
//                   |           |                  |
//              +----v----+ +----v----+             |
//              | fm_full | | fm_frac |             |
//              +----+----+ +----+----+             |
//                   |           |                  ^
//                   +-----v-----+                  |
//                         |                        |
//              WAITFOR(fm_full,fm_frac)            |
//                         |                        |
//                         v                        |
//                      +--v--+                WAITFOR(bc)
//                      | hm  |                     |
//                      +-----+                     |
//                         |                        |
//                    WAITFOR(hm)                   |
//                         |                        ^
//                      +--v--+                     |
//                      | bc  |                     |
//                      +-----+                     |
//                         |                        |
//                         v                        |
//                  MERGING COMPLETE?-------NO------+
//                         |
//                        YES
//                         |
//                         v
//                    END_WITH(bc)
//

struct hotsort_vk
{
  struct hotsort_vk_target_config config;

  uint32_t slab_keys;
  uint32_t key_val_size;
  uint32_t bs_slabs_log2_ru;
  uint32_t bc_slabs_log2_max;

  VkPipelineLayout pl;

  struct
  {
    uint32_t     count;
    VkPipeline * bs;
    VkPipeline * bc;
    VkPipeline * fm[3];
    VkPipeline * hm[3];
    VkPipeline * fill_in;
    VkPipeline * fill_out;
    VkPipeline * transpose;
    VkPipeline   all[];
  } pipelines;
};

//
//
//

struct hotsort_vk *
hotsort_vk_create(VkDevice                               device,
                  VkAllocationCallbacks const *          allocator,
                  VkPipelineCache                        pipeline_cache,
                  VkPipelineLayout                       pipeline_layout,
                  struct hotsort_vk_target const * const target)
{
  //
  // we reference these values a lot
  //
  uint32_t const bs_slabs_log2_ru  = msb_idx_u32(pow2_ru_u32(target->config.block.slabs));
  uint32_t const bc_slabs_log2_max = msb_idx_u32(pow2_rd_u32(target->config.block.slabs));

  //
  // how many kernels will be created?
  //
  uint32_t const count_bs    = bs_slabs_log2_ru + 1;
  uint32_t const count_bc    = bc_slabs_log2_max + 1;
  uint32_t       count_fm[3] = { 0 };
  uint32_t       count_hm[3] = { 0 };

  // guaranteed to be in range [0,2]
  for (uint32_t scale = target->config.merge.fm.scale_min;
       scale <= target->config.merge.fm.scale_max;
       scale++)
    {
      uint32_t fm_left = (target->config.block.slabs / 2) << scale;

      count_fm[scale] = msb_idx_u32(pow2_ru_u32(fm_left)) + 1;
    }

  // guaranteed to be in range [0,2]
  for (uint32_t scale = target->config.merge.hm.scale_min;
       scale <= target->config.merge.hm.scale_max;
       scale++)
    {
      count_hm[scale] = 1;
    }

  uint32_t const count_bc_fm_hm_fills_transpose =
    count_bc + count_fm[0] + count_fm[1] + count_fm[2] + count_hm[0] + count_hm[1] + count_hm[2] +
    3;  // fill_in + fill_out + transpose

  uint32_t const count_all = count_bs + count_bc_fm_hm_fills_transpose;

  //
  // allocate hotsort_vk
  //
  struct hotsort_vk * const hs = malloc(sizeof(*hs) + sizeof(VkPipeline *) * count_all);

  // copy the config from the target -- we need these values later
  hs->config = target->config;

  // save some frequently used calculated values
  hs->slab_keys         = target->config.slab.height << target->config.slab.width_log2;
  hs->key_val_size      = (target->config.dwords.key + target->config.dwords.val) * 4;
  hs->bs_slabs_log2_ru  = bs_slabs_log2_ru;
  hs->bc_slabs_log2_max = bc_slabs_log2_max;

  // save pipeline layout for vkCmdPushConstants()
  hs->pl = pipeline_layout;

  // save kernel count
  hs->pipelines.count = count_all;

  //
  // Prepare to create compute pipelines
  //
  VkComputePipelineCreateInfo cpci = {

    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = NULL,
    .flags = VK_PIPELINE_CREATE_DISPATCH_BASE,
    .stage = { .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .pNext               = NULL,
               .flags               = 0,
               .stage               = VK_SHADER_STAGE_COMPUTE_BIT,
               .module              = VK_NULL_HANDLE,
               .pName               = "main",
               .pSpecializationInfo = NULL },

    .layout             = pipeline_layout,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex  = 0
  };

  //
  // Set the subgroup size to what we expected when we built the HotSort target
  //
  VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT rssci = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
    .pNext = NULL,
    .requiredSubgroupSize = 0
  };

  //
  // Create a shader module, use it to create a pipeline... and
  // dispose of the shader module.
  //
  // BS        shaders have layout: (vout,vin)
  // FILL_IN   shaders have layout: (----,vin)
  // FILL_OUT  shaders have layout: (vout)
  // otherwise shaders have layout: (vout)
  //
  VkShaderModuleCreateInfo smci = { .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                    .pNext    = NULL,
                                    .flags    = 0,
                                    .codeSize = 0,
                                    .pCode    = NULL };

  //
  // Create a pipeline from each module
  //
  // FIXME(allanmac): an alternative layout would list the module
  // locations in the header enabling use of a parallelized pipeline
  // creation instruction.
  //
  uint32_t const * modules = target->modules;

  for (uint32_t ii = 0; ii < count_all; ii++)
    {
      uint32_t const module_dwords = *modules++;

      smci.codeSize = module_dwords * sizeof(*modules);
      smci.pCode    = modules;

      modules += module_dwords;

      //
      // DEBUG
      //
#if !defined(NDEBUG) && defined(HOTSORT_VK_PIPELINE_CODE_SIZE)
      fprintf(stderr, "%-38s ", "HOTSORT SHADER");
      fprintf(stderr, "(codeSize = %6zu) ... ", smci.codeSize);
#endif

      //
      // is subgroup size control active?
      //
      if (target->config.extensions.named.EXT_subgroup_size_control)
        {
          rssci.requiredSubgroupSize = 1 << target->config.slab.threads_log2;

          if (rssci.requiredSubgroupSize > 1)
            {
              cpci.stage.pNext = &rssci;
              cpci.stage.flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;
            }
          else
            {
              cpci.stage.pNext = NULL;
              cpci.stage.flags = 0;
            }
        }

      vk(CreateShaderModule(device, &smci, allocator, &cpci.stage.module));

      vk(CreateComputePipelines(device,
                                pipeline_cache,
                                1,
                                &cpci,
                                allocator,
                                hs->pipelines.all + ii));

      vkDestroyShaderModule(device, cpci.stage.module, allocator);

      //
      // DEBUG
      //
#if !defined(NDEBUG) && defined(HOTSORT_VK_PIPELINE_CODE_SIZE)
      fprintf(stderr, "OK\n");
#endif
    }

  //
  // initialize pointers to pipeline handles
  //
  VkPipeline * pipeline_next = hs->pipelines.all;

  // BS
  hs->pipelines.bs = pipeline_next;
  pipeline_next += count_bs;

  // BC
  hs->pipelines.bc = pipeline_next;
  pipeline_next += count_bc;

  // FM[0]
  hs->pipelines.fm[0] = count_fm[0] ? pipeline_next : NULL;
  pipeline_next += count_fm[0];

  // FM[1]
  hs->pipelines.fm[1] = count_fm[1] ? pipeline_next : NULL;
  pipeline_next += count_fm[1];

  // FM[2]
  hs->pipelines.fm[2] = count_fm[2] ? pipeline_next : NULL;
  pipeline_next += count_fm[2];

  // HM[0]
  hs->pipelines.hm[0] = count_hm[0] ? pipeline_next : NULL;
  pipeline_next += count_hm[0];

  // HM[1]
  hs->pipelines.hm[1] = count_hm[1] ? pipeline_next : NULL;
  pipeline_next += count_hm[1];

  // HM[2]
  hs->pipelines.hm[2] = count_hm[2] ? pipeline_next : NULL;
  pipeline_next += count_hm[2];

  // FILL_IN
  hs->pipelines.fill_in = pipeline_next;
  pipeline_next += 1;

  // FILL_OUT
  hs->pipelines.fill_out = pipeline_next;
  pipeline_next += 1;

  // TRANSPOSE
  hs->pipelines.transpose = pipeline_next;
  pipeline_next += 1;

  //
  // optionally dump pipeline stats
  //
#ifndef NDEBUG

#ifdef HOTSORT_VK_SHADER_INFO_AMD_STATISTICS
  if (target->config.extensions.named.AMD_shader_info)
    {
      vk_shader_info_amd_statistics(device, hs->pipelines.all, NULL, hs->pipelines.count);
    }
#endif
#ifdef HOTSORT_VK_SHADER_INFO_AMD_DISASSEMBLY
  if (target->config.extensions.named.AMD_shader_info)
    {
      vk_shader_info_amd_disassembly(device, hs->pipelines.all, NULL, hs->pipelines.count);
    }
#endif

#endif

  //
  // we're done
  //
  return hs;
}

//
//
//

void
hotsort_vk_release(VkDevice                            device,
                   VkAllocationCallbacks const * const allocator,
                   struct hotsort_vk * const           hs)
{
  for (uint32_t ii = 0; ii < hs->pipelines.count; ii++)
    {
      vkDestroyPipeline(device, hs->pipelines.all[ii], allocator);
    }

  free(hs);
}

//
//
//

void
hotsort_vk_pad(struct hotsort_vk const * const hs,
               uint32_t const                  count,
               uint32_t * const                slabs_in,
               uint32_t * const                padded_in,
               uint32_t * const                padded_out)
{
  //
  // round up the count to slabs
  //
  uint32_t const slabs_ru     = (count + hs->slab_keys - 1) / hs->slab_keys;
  uint32_t const blocks       = slabs_ru / hs->config.block.slabs;
  uint32_t const block_slabs  = blocks * hs->config.block.slabs;
  uint32_t const slabs_ru_rem = slabs_ru - block_slabs;
  uint32_t const slabs_ru_rem_ru =
    MIN_MACRO(uint32_t, pow2_ru_u32(slabs_ru_rem), hs->config.block.slabs);

  *slabs_in   = slabs_ru;
  *padded_in  = (block_slabs + slabs_ru_rem_ru) * hs->slab_keys;
  *padded_out = *padded_in;

  //
  // will merging be required?
  //
  if (slabs_ru > hs->config.block.slabs)
    {
      // more than one block
      uint32_t const blocks_lo       = pow2_rd_u32(blocks);
      uint32_t const block_slabs_lo  = blocks_lo * hs->config.block.slabs;
      uint32_t const block_slabs_rem = slabs_ru - block_slabs_lo;

      if (block_slabs_rem > 0)
        {
          uint32_t const block_slabs_rem_ru = pow2_ru_u32(block_slabs_rem);

          uint32_t const block_slabs_hi =
            MAX_MACRO(uint32_t,
                      block_slabs_rem_ru,
                      blocks_lo << (1 - hs->config.merge.fm.scale_min));

          uint32_t const block_slabs_padded_out =
            MIN_MACRO(uint32_t,
                      block_slabs_lo + block_slabs_hi,
                      block_slabs_lo * 2);  // clamp non-pow2 blocks

          *padded_out = block_slabs_padded_out * hs->slab_keys;
        }
    }
}

//
//
//

static void
hs_cmd_transpose(VkCommandBuffer cb, struct hotsort_vk const * const hs, uint32_t const bx_ru)
{
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.transpose[0]);

  vkCmdDispatch(cb, bx_ru, 1, 1);
}

//
//
//

static void
hs_cmd_fill_in(VkCommandBuffer                 cb,
               struct hotsort_vk const * const hs,
               uint32_t const                  from_slab,
               uint32_t const                  to_slab)
{
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.fill_in[0]);

  uint32_t const slabs_ru = to_slab - from_slab;

  vkCmdDispatchBase(cb, from_slab, 0, 0, slabs_ru, 1, 1);
}

static void
hs_cmd_fill_out(VkCommandBuffer                 cb,
                struct hotsort_vk const * const hs,
                uint32_t const                  from_slab,
                uint32_t const                  to_slab)
{
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.fill_out[0]);

  uint32_t const slabs_ru = to_slab - from_slab;

  vkCmdDispatchBase(cb, from_slab, 0, 0, slabs_ru, 1, 1);
}

//
//
//

static void
hs_cmd_bc(VkCommandBuffer                 cb,
          struct hotsort_vk const * const hs,
          uint32_t const                  down_slabs,
          uint32_t const                  clean_slabs_log2)
{
  // block clean the minimal number of down_slabs_log2 spans
  uint32_t const frac_ru = (1u << clean_slabs_log2) - 1;
  uint32_t const full_bc = (down_slabs + frac_ru) >> clean_slabs_log2;

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.bc[clean_slabs_log2]);

  vkCmdDispatch(cb, full_bc, 1, 1);
}

//
//
//

static uint32_t
hs_cmd_hm(VkCommandBuffer                 cb,
          struct hotsort_vk const * const hs,
          uint32_t const                  down_slabs,
          uint32_t const                  clean_slabs_log2)
{
  // how many scaled half-merge spans are there?
  uint32_t const frac_ru = (1 << clean_slabs_log2) - 1;
  uint32_t const spans   = (down_slabs + frac_ru) >> clean_slabs_log2;

  // for now, just clamp to the max
  uint32_t const log2_rem   = clean_slabs_log2 - hs->bc_slabs_log2_max;
  uint32_t const scale_log2 = MIN_MACRO(uint32_t, hs->config.merge.hm.scale_max, log2_rem);
  uint32_t const log2_out   = log2_rem - scale_log2;

  // size the grid
  uint32_t const slab_span = hs->config.slab.height << log2_out;

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.hm[scale_log2][0]);

  vkCmdDispatch(cb, slab_span, spans, 1);

  return log2_out;
}

//
// FIXME -- some of this logic can be skipped if BS is a power-of-two
//

static uint32_t
hs_cmd_fm(VkCommandBuffer                 cb,
          struct hotsort_vk const * const hs,
          uint32_t const                  bx_ru,
          uint32_t * const                down_slabs,
          uint32_t const                  up_scale_log2)
{
  //
  // FIXME OPTIMIZATION: in previous HotSort launchers it's sometimes
  // a performance win to bias toward launching the smaller flip merge
  // kernel in order to get more warps in flight (increased
  // occupancy).  This is useful when merging small numbers of slabs.
  //
  // Note that HS_FM_SCALE_MIN will always be 0 or 1.
  //
  // So, for now, just clamp to the max until there is a reason to
  // restore the fancier and probably low-impact approach.
  //
  uint32_t const scale_log2 = MIN_MACRO(uint32_t, hs->config.merge.fm.scale_max, up_scale_log2);
  uint32_t const clean_log2 = up_scale_log2 - scale_log2;

  // number of slabs in a full-sized scaled flip-merge span
  uint32_t const full_span_slabs = hs->config.block.slabs << up_scale_log2;

  // how many full-sized scaled flip-merge spans are there?
  uint32_t full_fm = bx_ru / full_span_slabs;
  uint32_t frac_fm = 0;

  // initialize down_slabs
  *down_slabs = full_fm * full_span_slabs;

  // how many half-size scaled + fractional scaled spans are there?
  uint32_t const span_rem        = bx_ru - *down_slabs;
  uint32_t const half_span_slabs = full_span_slabs >> 1;

  // if we have over a half-span then fractionally merge it
  if (span_rem > half_span_slabs)
    {
      // the remaining slabs will be cleaned
      *down_slabs += span_rem;

      uint32_t const frac_rem      = span_rem - half_span_slabs;
      uint32_t const frac_rem_pow2 = pow2_ru_u32(frac_rem);

      if (frac_rem_pow2 >= half_span_slabs)
        {
          // bump it up to a full span
          full_fm += 1;
        }
      else
        {
          // otherwise, add fractional
          frac_fm = MAX_MACRO(uint32_t, 1, frac_rem_pow2 >> clean_log2);
        }
    }

  //
  // Size the grid
  //
  // The simplifying choices below limit the maximum keys that can be
  // sorted with this grid scheme to around ~2B.
  //
  //   .x : slab height << clean_log2  -- this is the slab span
  //   .y : [1...65535]                -- this is the slab index
  //   .z : ( this could also be used to further expand .y )
  //
  // Note that OpenCL declares a grid in terms of global threads and
  // not grids and blocks
  //

  //
  // size the grid
  //
  uint32_t const slab_span = hs->config.slab.height << clean_log2;

  if (full_fm > 0)
    {
      uint32_t const full_idx = hs->bs_slabs_log2_ru - 1 + scale_log2;

      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.fm[scale_log2][full_idx]);

      vkCmdDispatch(cb, slab_span, full_fm, 1);
    }

  if (frac_fm > 0)
    {
      vkCmdBindPipeline(cb,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        hs->pipelines.fm[scale_log2][msb_idx_u32(frac_fm)]);

      vkCmdDispatchBase(cb, 0, full_fm, 0, slab_span, 1, 1);
    }

  return clean_log2;
}

//
//
//

static void
hs_cmd_bs(VkCommandBuffer cb, struct hotsort_vk const * const hs, uint32_t const padded_in)
{
  uint32_t const slabs_in = padded_in / hs->slab_keys;
  uint32_t const full_bs  = slabs_in / hs->config.block.slabs;
  uint32_t const frac_bs  = slabs_in - full_bs * hs->config.block.slabs;

  if (full_bs > 0)
    {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.bs[hs->bs_slabs_log2_ru]);

      vkCmdDispatch(cb, full_bs, 1, 1);
    }

  if (frac_bs > 0)
    {
      uint32_t const frac_idx          = msb_idx_u32(frac_bs);
      uint32_t const full_to_frac_log2 = hs->bs_slabs_log2_ru - frac_idx;

      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, hs->pipelines.bs[msb_idx_u32(frac_bs)]);

      vkCmdDispatchBase(cb, full_bs << full_to_frac_log2, 0, 0, 1, 1, 1);
    }
}

//
//
//

void
hotsort_vk_sort(VkCommandBuffer                            cb,
                struct hotsort_vk const * const            hs,
                struct hotsort_vk_ds_offsets const * const offsets,
                uint32_t const                             count,
                uint32_t const                             padded_in,
                uint32_t const                             padded_out,
                bool const                                 linearize)
{
  //
  // append the push constants
  //
  size_t const kv_size = (hs->config.dwords.key + hs->config.dwords.val) * sizeof(uint32_t);

  struct hotsort_vk_push const push = { .kv_offset_in  = (uint32_t)(offsets->in / kv_size),
                                        .kv_offset_out = (uint32_t)(offsets->out / kv_size),
                                        .kv_count      = count };

  vkCmdPushConstants(cb,
                     hs->pl,
                     HOTSORT_VK_PUSH_CONSTANT_RANGE_STAGE_FLAGS,
                     HOTSORT_VK_PUSH_CONSTANT_RANGE_OFFSET,
                     HOTSORT_VK_PUSH_CONSTANT_RANGE_SIZE,
                     &push);

  //
  // The input and output buffers may need to be initialized with max
  // value keys.
  //
  //   - pre-sort  fill needs to happen before bs()
  //   - pre-merge fill needs to happen before fm()
  //
  bool const     is_in_place       = hs->config.is_in_place && (offsets->in == offsets->out);
  uint32_t const padded_pre_sort   = is_in_place ? padded_out : padded_in;
  bool const     is_pre_sort_reqd  = padded_pre_sort > count;
  bool const     is_pre_merge_reqd = !is_in_place && (padded_out > padded_in);

  //
  // pre-sort fill?
  //
  // Note: If there is either 0 or 1 key then there is nothing to do after padding the slab.
  //
  if (is_pre_sort_reqd)
    {
      uint32_t const from_slab = count / hs->slab_keys;
      uint32_t const to_slab   = padded_pre_sort / hs->slab_keys;

      hs_cmd_fill_in(cb, hs, from_slab, to_slab);

      if (count <= 1)
        return;

      vk_barrier_compute_w_to_compute_r(cb);
    }

  //
  // sort blocks of slabs... after hs_keyset_pre_sort()
  //
  uint32_t const bx_ru = (count + hs->slab_keys - 1) / hs->slab_keys;

  hs_cmd_bs(cb, hs, padded_in);

  //
  // if this was a single bs block then there is no merging
  //
  if (bx_ru > hs->config.block.slabs)
    {
      //
      // pre-merge fill?
      //
      if (is_pre_merge_reqd)
        {
          uint32_t const from_slab = padded_in / hs->slab_keys;
          uint32_t const to_slab   = padded_out / hs->slab_keys;

          hs_cmd_fill_out(cb, hs, from_slab, to_slab);
        }

      //
      // merge sorted spans of slabs until done...
      //
      uint32_t up_scale_log2 = 1;

      while (true)
        {
          uint32_t down_slabs;

          //
          // flip merge slabs -- return span of slabs that must be cleaned
          //
          vk_barrier_compute_w_to_compute_r(cb);

          uint32_t clean_slabs_log2 = hs_cmd_fm(cb, hs, bx_ru, &down_slabs, up_scale_log2);

          //
          // if span is greater than largest slab block cleaner then
          // half merge
          //
          while (clean_slabs_log2 > hs->bc_slabs_log2_max)
            {
              vk_barrier_compute_w_to_compute_r(cb);

              clean_slabs_log2 = hs_cmd_hm(cb, hs, down_slabs, clean_slabs_log2);
            }

          //
          // launch clean slab grid -- is it the final launch?
          //
          vk_barrier_compute_w_to_compute_r(cb);

          hs_cmd_bc(cb, hs, down_slabs, clean_slabs_log2);

          //
          // was this the final block clean?
          //
          if (((uint32_t)hs->config.block.slabs << up_scale_log2) >= bx_ru)
            {
              break;
            }

          //
          // otherwise, merge twice as many slabs
          //
          up_scale_log2 += 1;
        }
    }

  // slabs or linear?
  if (linearize)
    {
      vk_barrier_compute_w_to_compute_r(cb);

      hs_cmd_transpose(cb, hs, bx_ru);
    }
}

//
//
//
