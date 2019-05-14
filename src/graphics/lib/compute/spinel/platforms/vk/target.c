// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "target.h"

#include <assert.h>  // REMOVE ME once we get DUTD integrated with scheduler
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/vk_assert.h"
#include "device.h"
#include "target_config.h"

//
// Verify pipeline count matches
//

#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) +1

STATIC_ASSERT_MACRO_1((0 + SPN_TARGET_P_EXPAND()) == SPN_TARGET_P_COUNT);

//
// Verify descriptor set count matches
//

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) +1

STATIC_ASSERT_MACRO_1((0 + SPN_TARGET_DS_EXPAND()) == SPN_TARGET_DS_COUNT);

//
// AMD shader info
//

#if defined(SPN_SHADER_INFO_AMD_STATISTICS) || defined(SPN_SHADER_INFO_AMD_DISASSEMBLY)

#include "common/vk/vk_shader_info_amd.h"

#define SPN_PIPELINE_NAMES_DEFINE

#endif

/////////////////////////////////////////////////////////////////
//
// VULKAN DESCRIPTOR SET & PIPELINE EXPANSIONS
//

#define SPN_TARGET_DS_E(id) spn_target_ds_idx_##id

typedef enum spn_target_ds_e
{

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_TARGET_DS_E(_ds_id) = _ds_idx,

  SPN_TARGET_DS_EXPAND()

} spn_target_ds_e;

//
//
//

struct spn_target_pipelines
{
#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) VkPipeline _p_id;

  SPN_TARGET_P_EXPAND()
};

//
//
//

#define SPN_TARGET_PIPELINE_NAMES_DEFINE

#ifdef SPN_TARGET_PIPELINE_NAMES_DEFINE

static char const * const spn_target_p_names[] = {
#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) #_p_id,

  SPN_TARGET_P_EXPAND()};

#define SPN_TARGET_PIPELINE_NAMES spn_target_p_names

#else

#define SPN_TARGET_PIPELINE_NAMES NULL

#endif

//
//
//

struct spn_target_pl
{
#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) VkPipelineLayout _p_id;

  SPN_TARGET_P_EXPAND()
};

//
// Create VkDescriptorSetLayoutBinding structs
//

#undef SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                         \
  {.binding            = _d_idx,                                                                   \
   .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,                                        \
   .descriptorCount    = 1,                                                                        \
   .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,                                              \
   .pImmutableSamplers = NULL},

#undef SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                          \
  {.binding            = _d_idx,                                                                   \
   .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,                                         \
   .descriptorCount    = 1,                                                                        \
   .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,                                              \
   .pImmutableSamplers = NULL},

#define SPN_TARGET_DSLB_NAME(_ds_id) spn_target_dslb_##_ds_id

#define SPN_TARGET_DSLB_CREATE(_ds_id, ...)                                                        \
  static const VkDescriptorSetLayoutBinding SPN_TARGET_DSLB_NAME(_ds_id)[] = {__VA_ARGS__};

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, ...) SPN_TARGET_DSLB_CREATE(_ds_id, __VA_ARGS__)

SPN_TARGET_DS_EXPAND();

//
// Create VkDescriptorSetLayoutCreateInfo structs
//

#define SPN_TARGET_DSLCI(dslb)                                                                     \
  {                                                                                                \
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = NULL, .flags = 0,       \
    .bindingCount = ARRAY_LENGTH_MACRO(dslb), .pBindings = dslb                                    \
  }

#define SPN_TARGET_DSLCI_NAME(_ds_id) spn_target_dslci_##_ds_id

#define SPN_TARGET_DSLCI_CREATE(_ds_id)                                                            \
  static const VkDescriptorSetLayoutCreateInfo SPN_TARGET_DSLCI_NAME(_ds_id)[] = {                 \
    SPN_TARGET_DSLCI(SPN_TARGET_DSLB_NAME(_ds_id))}

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_TARGET_DSLCI_CREATE(_ds_id);

SPN_TARGET_DS_EXPAND()

//
// DSL -- descriptor set layout
//

#define SPN_TARGET_DSL_DECLARE(_ds_id) VkDescriptorSetLayout _ds_id

struct spn_target_dsl
{
#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_TARGET_DSL_DECLARE(_ds_id);

  SPN_TARGET_DS_EXPAND()
};

//
// DUTD -- descriptor update template data
//

#undef SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                         \
  struct                                                                                           \
  {                                                                                                \
    VkDescriptorBufferInfo entry;                                                                  \
  } _d_id;

#undef SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                          \
  struct                                                                                           \
  {                                                                                                \
    VkDescriptorImageInfo entry;                                                                   \
  } _d_id;

#define SPN_TARGET_DUTD_NAME(_ds_id) spn_target_dutd_##_ds_id

#define SPN_TARGET_DUTD_DEFINE(_ds_id, _ds)                                                        \
  struct SPN_TARGET_DUTD_NAME(_ds_id)                                                              \
  {                                                                                                \
    _ds                                                                                            \
  };

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_TARGET_DUTD_DEFINE(_ds_id, _ds)

SPN_TARGET_DS_EXPAND()

//
// DUTDP -- DUTD pool
//
//   dutds : array of all DUTD structs
//   pool  : LIFO stack of indices to available DUTD structs
//   rem   : remaining number of DUTD indices in LIFO pool
//   size  : size of dutds array
//

#define SPN_TARGET_DUTDP_DEFINE(_ds_id)                                                            \
  struct                                                                                           \
  {                                                                                                \
    struct SPN_TARGET_DUTD_NAME(_ds_id) * dutds;                                                   \
    uint32_t *        pool;                                                                        \
    VkDescriptorSet * ds;                                                                          \
    uint32_t          rem;                                                                         \
    uint32_t          size;                                                                        \
  } _ds_id;

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_TARGET_DUTDP_DEFINE(_ds_id)

struct spn_target_dutdp
{
  SPN_TARGET_DS_EXPAND()

  void * mem;  // single allocation
};

//
// DUTE -- descriptor update template entry
//

#undef SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                         \
  {.dstBinding      = _d_idx,                                                                      \
   .dstArrayElement = 0,                                                                           \
   .descriptorCount = 1,                                                                           \
   .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,                                           \
   .offset          = offsetof(struct SPN_TARGET_DUTD_NAME(_ds_id), _d_id),                        \
   .stride          = 0},

#undef SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                          \
  {.dstBinding      = _d_idx,                                                                      \
   .dstArrayElement = 0,                                                                           \
   .descriptorCount = 1,                                                                           \
   .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,                                            \
   .offset          = offsetof(struct SPN_TARGET_DUTD_NAME(_ds_id), _d_id),                        \
   .stride          = 0},

#define SPN_TARGET_DUTE_NAME(_ds_id) spn_target_dute_##_ds_id

#define SPN_TARGET_DUTE_CREATE(_ds_id, ...)                                                        \
  static const VkDescriptorUpdateTemplateEntry SPN_TARGET_DUTE_NAME(_ds_id)[] = {__VA_ARGS__};

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, ...) SPN_TARGET_DUTE_CREATE(_ds_id, __VA_ARGS__)

SPN_TARGET_DS_EXPAND()

//
// DUT -- descriptor update template
//

#define SPN_TARGET_DUT_DECLARE(_ds_id) VkDescriptorUpdateTemplate _ds_id

struct spn_target_dut
{
#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_TARGET_DUT_DECLARE(_ds_id);

  SPN_TARGET_DS_EXPAND()
};

//
// DP -- define descriptor pools
//

#define SPN_TARGET_DP_DECLARE(_ds_id) VkDescriptorPool _ds_id

struct spn_target_dp
{
#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_TARGET_DP_DECLARE(_ds_id);

  SPN_TARGET_DS_EXPAND()
};

#undef SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                         \
  {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1},

#undef SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                          \
  {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1},

#define SPN_TARGET_DPS_NAME(_ds_id) spn_target_dps_##_ds_id

#define SPN_TARGET_DPS_DEFINE(_ds_id, ...)                                                         \
  static VkDescriptorPoolSize const SPN_TARGET_DPS_NAME(_ds_id)[] = {__VA_ARGS__};

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, ...) SPN_TARGET_DPS_DEFINE(_ds_id, __VA_ARGS__)

SPN_TARGET_DS_EXPAND()

//
// Define pipeline descriptor update types
//

#undef SPN_TARGET_VK_DS
#define SPN_TARGET_VK_DS(_p_id, _ds_idx, _ds_id) struct SPN_TARGET_DUTD_NAME(_ds_id) _ds_id;

#undef SPN_TARGET_VK_PUSH
#define SPN_TARGET_VK_PUSH(_p_id, _p_pc)

#define SPN_TARGET_PDUTD_NAME(_p_id) spn_target_pdutd_##_p_id

#define SPN_TARGET_PDUTD_DEFINE(_p_id, _p_descs)                                                   \
  struct SPN_TARGET_PDUTD_NAME(_p_id)                                                              \
  {                                                                                                \
    _p_descs                                                                                       \
  };

#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) SPN_TARGET_PDUTD_DEFINE(_p_id, _p_descs)

SPN_TARGET_P_EXPAND()

//
//
//

struct spn_target
{
  struct spn_target_config config;

  union
  {
    struct spn_target_dsl named;  // descriptor set layouts
    VkDescriptorSetLayout array[SPN_TARGET_DS_COUNT];
  } dsl;

  union
  {
    struct spn_target_dut      named;  // descriptor update templates
    VkDescriptorUpdateTemplate array[SPN_TARGET_DS_COUNT];
  } dut;

  union
  {
    struct spn_target_dp named;  // descriptor pools
    VkDescriptorPool     array[SPN_TARGET_DS_COUNT];
  } dp;

  struct spn_target_dutdp dutdp;  // descriptor update template data pools

  union
  {
    struct spn_target_pl named;  // pipeline layouts
    VkPipelineLayout     array[SPN_TARGET_P_COUNT];
  } pl;

  union
  {
    struct spn_target_pipelines named;  // pipelines
    VkPipeline                  array[SPN_TARGET_P_COUNT];
  } p;
};

//
//
//

struct spn_target_config const *
spn_target_get_config(struct spn_target const * const target)
{
  return &target->config;
}

//
//
//

static void
spn_target_dutdp_free(struct spn_target * const target, struct spn_device_vk * const vk)
{
#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                               \
  {                                                                                                \
    uint32_t const   size = target->dutdp._ds_id.size;                                             \
    VkDescriptorPool dp   = target->dp.array[_ds_idx];                                             \
                                                                                                   \
    vk(FreeDescriptorSets(vk->d, dp, size, target->dutdp._ds_id.ds));                              \
  }

  SPN_TARGET_DS_EXPAND()

  free(target->dutdp.mem);
}

//
//
//

void
spn_target_dispose(struct spn_target * const target, struct spn_device_vk * const vk)
{
  //
  // PIPELINE
  //
  for (uint32_t ii = 0; ii < SPN_TARGET_P_COUNT; ii++)
    {
      vkDestroyPipeline(vk->d, target->p.array[ii], vk->ac);
    }

  //
  // PL
  //
  for (uint32_t ii = 0; ii < SPN_TARGET_P_COUNT; ii++)
    {
      vkDestroyPipelineLayout(vk->d, target->pl.array[ii], vk->ac);
    }

  //
  // DUTDP
  //
  spn_target_dutdp_free(target, vk);

  //
  // DP
  //
  for (uint32_t ii = 0; ii < SPN_TARGET_DS_COUNT; ii++)
    {
      vkDestroyDescriptorPool(vk->d, target->dp.array[ii], vk->ac);
    }

  //
  // DUT
  //
  for (uint32_t ii = 0; ii < SPN_TARGET_DS_COUNT; ii++)
    {
      vkDestroyDescriptorUpdateTemplate(vk->d, target->dut.array[ii], vk->ac);
    }

  //
  // DSL
  //
  for (uint32_t ii = 0; ii < SPN_TARGET_DS_COUNT; ii++)
    {
      vkDestroyDescriptorSetLayout(vk->d, target->dsl.array[ii], vk->ac);
    }

  //
  // SPN
  //
  free(target);
}

//
//
//

struct spn_target *
spn_target_create(struct spn_device_vk * const          vk,
                  struct spn_target_image const * const target_image)
{
  //
  // allocate spn_vk
  //
  struct spn_target * target = malloc(sizeof(*target));

  // save config
  memcpy(&target->config, &target_image->config, sizeof(target->config));

  //
  // DSL -- create descriptor set layout
  //
#define SPN_TARGET_DSL_CREATE(id)                                                                  \
  vk(CreateDescriptorSetLayout(vk->d, SPN_TARGET_DSLCI_NAME(id), vk->ac, &target->dsl.named.id))

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(idx, id, desc) SPN_TARGET_DSL_CREATE(id);

  SPN_TARGET_DS_EXPAND();

  //
  // DUT -- create descriptor update templates
  //
  VkDescriptorUpdateTemplateCreateInfo dutci = {
    .sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
    .pNext                      = NULL,
    .flags                      = 0,
    .descriptorUpdateEntryCount = 0,
    .pDescriptorUpdateEntries   = NULL,
    .templateType               = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
    .descriptorSetLayout        = VK_NULL_HANDLE,
    .pipelineBindPoint          = VK_PIPELINE_BIND_POINT_COMPUTE,
    .pipelineLayout             = VK_NULL_HANDLE,
    .set                        = 0};

#define SPN_TARGET_DUT_CREATE(id)                                                                  \
  dutci.descriptorUpdateEntryCount = ARRAY_LENGTH_MACRO(SPN_TARGET_DUTE_NAME(id));                 \
  dutci.pDescriptorUpdateEntries   = SPN_TARGET_DUTE_NAME(id);                                     \
  dutci.descriptorSetLayout        = target->dsl.named.id;                                         \
  vk(CreateDescriptorUpdateTemplate(vk->d, &dutci, vk->ac, &target->dut.named.id))

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(idx, id, desc) SPN_TARGET_DUT_CREATE(id);

  SPN_TARGET_DS_EXPAND();

  //
  // DP -- create descriptor pools
  //
  VkDescriptorPoolCreateInfo dpci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                     .pNext = NULL,
                                     .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT};

#define SPN_TARGET_DP_CREATE(idx, id)                                                              \
  {                                                                                                \
    dpci.maxSets       = target->config.ds.id.sets;                                                \
    dpci.poolSizeCount = ARRAY_LENGTH_MACRO(SPN_TARGET_DPS_NAME(id));                              \
    dpci.pPoolSizes    = SPN_TARGET_DPS_NAME(id);                                                  \
                                                                                                   \
    vk(CreateDescriptorPool(vk->d, &dpci, vk->ac, &target->dp.named.id));                          \
  }

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(idx, id, desc) SPN_TARGET_DP_CREATE(idx, id);

  SPN_TARGET_DS_EXPAND();

  //
  // DUTD POOLS
  //
  size_t dutd_total = 0;

  // array of pointers + array of structs
#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                               \
  target->dutdp._ds_id.rem  = target->config.ds._ds_id.sets;                                       \
  target->dutdp._ds_id.size = target->config.ds._ds_id.sets;                                       \
  dutd_total += target->config.ds._ds_id.sets *                                                    \
                (sizeof(*target->dutdp._ds_id.dutds) + sizeof(*target->dutdp._ds_id.pool) +        \
                 sizeof(*target->dutdp._ds_id.ds));

  SPN_TARGET_DS_EXPAND();

  void * dutd_base = malloc(dutd_total);

  // save the memory blob
  target->dutdp.mem = dutd_base;

  VkDescriptorSetAllocateInfo dsai = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext = NULL,
    // .descriptorPool     = target->dp.array[idx],
    .descriptorSetCount = 1,
    // .pSetLayouts        = target->dsl.array + idx
  };

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                                \
  {                                                                                                 \
    uint32_t const size = target->dutdp._ds_id.size;                                                \
                                                                                                    \
    target->dutdp._ds_id.dutds = dutd_base;                                                         \
    dutd_base = ((uint8_t *)dutd_base + sizeof(*target->dutdp._ds_id.dutds) * size);                \
                                                                                                    \
    target->dutdp._ds_id.pool = dutd_base;                                                          \
    dutd_base                 = ((uint8_t *)dutd_base + sizeof(*target->dutdp._ds_id.pool) * size); \
                                                                                                    \
    target->dutdp._ds_id.ds = dutd_base;                                                            \
    dutd_base               = ((uint8_t *)dutd_base + sizeof(*target->dutdp._ds_id.ds) * size);     \
                                                                                                    \
    dsai.descriptorPool = target->dp.array[_ds_idx];                                                \
    dsai.pSetLayouts    = target->dsl.array + _ds_idx;                                              \
                                                                                                    \
    for (uint32_t ii = 0; ii < size; ii++)                                                          \
      {                                                                                             \
        target->dutdp._ds_id.pool[ii] = ii;                                                         \
                                                                                                    \
        vkAllocateDescriptorSets(vk->d, &dsai, target->dutdp._ds_id.ds + ii);                       \
      }                                                                                             \
  }

  SPN_TARGET_DS_EXPAND();

  //
  // PL -- create pipeline layouts
  //
  VkPushConstantRange pcr[] = {{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 0}};

  VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                     .pNext = NULL,
                                     .flags = 0,
                                     .setLayoutCount         = 0,
                                     .pSetLayouts            = NULL,
                                     .pushConstantRangeCount = 0,
                                     .pPushConstantRanges    = NULL};

#if defined(__Fuchsia__)  // TEMPORARILY FOR ARM TARGETS WITH A DESC SET LIMIT OF 4

  bool const p_ok[SPN_TARGET_P_COUNT] = {[12] = true};  // all false except 12

#else

#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(...) true,
  bool const p_ok[SPN_TARGET_P_COUNT] = {SPN_TARGET_P_EXPAND()};

#endif

#ifdef SPN_TARGET_PIPELINE_NAMES_DEFINE
#define SPN_PLCI_DEBUG(_p_idx)                                                                     \
  fprintf(stderr,                                                                                  \
          "plci.setLayoutCount[%-35s] = %2u  (%s)\n",                                              \
          SPN_TARGET_PIPELINE_NAMES[_p_idx],                                                       \
          plci.setLayoutCount,                                                                     \
          p_ok[_p_idx] ? "OK" : "SKIP!")
#else
#define SPN_PLCI_DEBUG(_p_idx)
#endif

#undef SPN_TARGET_VK_DS
#define SPN_TARGET_VK_DS(_p_id, _ds_idx, _ds_id) target->dsl.named._ds_id,

#define SPN_TARGET_PL_CREATE(_p_idx, _p_id, ...)                                                   \
  {                                                                                                \
    uint32_t const pps = target->config.p.push_sizes.array[_p_idx];                                \
                                                                                                   \
    if (pps == 0)                                                                                  \
      {                                                                                            \
        plci.pushConstantRangeCount = 0;                                                           \
        plci.pPushConstantRanges    = NULL;                                                        \
      }                                                                                            \
    else                                                                                           \
      {                                                                                            \
        pcr[0].size                 = pps;                                                         \
        plci.pushConstantRangeCount = 1;                                                           \
        plci.pPushConstantRanges    = pcr;                                                         \
      }                                                                                            \
                                                                                                   \
    const VkDescriptorSetLayout dsls[] = {__VA_ARGS__};                                            \
                                                                                                   \
    plci.setLayoutCount = ARRAY_LENGTH_MACRO(dsls);                                                \
    plci.pSetLayouts    = dsls;                                                                    \
                                                                                                   \
    SPN_PLCI_DEBUG(_p_idx);                                                                        \
                                                                                                   \
    if (p_ok[_p_idx])                                                                              \
      {                                                                                            \
        vk(CreatePipelineLayout(vk->d, &plci, vk->ac, &target->pl.named._p_id));                   \
      }                                                                                            \
  }

#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, ...) SPN_TARGET_PL_CREATE(_p_idx, _p_id, __VA_ARGS__)

  SPN_TARGET_P_EXPAND();

  //
  // create all the compute pipelines by reusing this info
  //
  VkComputePipelineCreateInfo cpci = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = NULL,
    .flags = VK_PIPELINE_CREATE_DISPATCH_BASE,  // | VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
    .stage = {.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .pNext               = NULL,
              .flags               = 0,
              .stage               = VK_SHADER_STAGE_COMPUTE_BIT,
              .module              = VK_NULL_HANDLE,
              .pName               = "main",
              .pSpecializationInfo = NULL},
    // .layout             = VK_NULL_HANDLE, // target->pl.layout.vout_vin,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex  = 0};

  //
  // Create a shader module, use it to create a pipeline... and
  // dispose of the shader module.
  //
  VkShaderModuleCreateInfo smci = {.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                   .pNext    = NULL,
                                   .flags    = 0,
                                   .codeSize = 0,
                                   .pCode    = (uint32_t const *)target_image->modules};

  for (uint32_t ii = 0; ii < SPN_TARGET_P_COUNT; ii++)
    {
      smci.pCode += smci.codeSize / sizeof(*smci.pCode);  // bytes to words
      smci.codeSize = NTOHL_MACRO(smci.pCode[0]);
      smci.pCode += 1;

      fprintf(stderr, "%-35s ", SPN_TARGET_PIPELINE_NAMES[ii]);

      fprintf(stderr, "(codeSize = %6zu) ... ", smci.codeSize);

      if (!p_ok[ii])
        {
          fprintf(stderr, "SKIP!\n");
          continue;
        }

      vk(CreateShaderModule(vk->d, &smci, vk->ac, &cpci.stage.module));

      cpci.layout = target->pl.array[ii];

      vk(CreateComputePipelines(vk->d, vk->pc, 1, &cpci, vk->ac, target->p.array + ii));

      vkDestroyShaderModule(vk->d, cpci.stage.module, vk->ac);

      fprintf(stderr, "OK\n");
    }

    //
    // optionally dump pipeline stats on AMD devices
    //
#ifdef SPN_TARGET_SHADER_INFO_AMD_STATISTICS
  vk_shader_info_amd_statistics(vk->d,
                                target->p.array,
                                SPN_TARGET_PIPELINE_NAMES,
                                SPN_TARGET_P_COUNT);
#endif
#ifdef SPN_TARGET_SHADER_INFO_AMD_DISASSEMBLY
  vk_shader_info_amd_disassembly(vk->d,
                                 target->p.array,
                                 SPN_TARGET_PIPELINE_NAMES,
                                 SPN_TARGET_P_COUNT);
#endif

  //
  // Done...
  //

  return target;
}

//
// Descriptor set operations:
//
//   1. Schedule if there isn't a ds/dutd available.
//
//      Note that there will always the same number of descriptor sets
//      and dutds in the pool so availability of one implies
//      availability of the other.
//
//   2. Acquire a descriptor set
//
//   3. Update the descriptor set
//
//   4. Release the dutd back to its pool.
//

//
// Acquire/Release the descriptor set
//

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                               \
  SPN_TARGET_DS_ACQUIRE_PROTO(_ds_id)                                                              \
  {                                                                                                \
    while (target->dutdp._ds_id.rem == 0)                                                          \
      {                                                                                            \
        spn_device_wait(device);                                                                   \
      }                                                                                            \
    ds->idx = target->dutdp._ds_id.pool[--target->dutdp._ds_id.rem];                               \
  }                                                                                                \
                                                                                                   \
  SPN_TARGET_DS_RELEASE_PROTO(_ds_id)                                                              \
  {                                                                                                \
    target->dutdp._ds_id.pool[target->dutdp._ds_id.rem++] = ds.idx;                                \
  }

SPN_TARGET_DS_EXPAND()

//
// Get reference to descriptor update template
//

#undef SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                         \
  SPN_TARGET_DS_GET_PROTO_STORAGE_BUFFER(_ds_id, _d_id)                                            \
  {                                                                                                \
    return &target->dutdp._ds_id.dutds[ds.idx]._d_id.entry;                                        \
  }

#undef SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                          \
  SPN_TARGET_DS_GET_PROTO_STORAGE_IMAGE(_ds_id, _d_id)                                             \
  {                                                                                                \
    return &target->dutdp._ds_id.dutds[ds.idx]._d_id.entry;                                        \
  }

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) _ds

SPN_TARGET_DS_EXPAND()

//
// Update the descriptor set
//

#undef SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                               \
  SPN_TARGET_DS_UPDATE_PROTO(_ds_id)                                                               \
  {                                                                                                \
    vkUpdateDescriptorSetWithTemplate(vk->d,                                                       \
                                      target->dutdp._ds_id.ds[ds.idx],                             \
                                      target->dut.named._ds_id,                                    \
                                      target->dutdp._ds_id.dutds + ds.idx);                        \
  }

SPN_TARGET_DS_EXPAND()

//
// Bind descriptor set to command buffer
//

#undef SPN_TARGET_VK_DS
#define SPN_TARGET_VK_DS(_p_id, _ds_idx, _ds_id)                                                   \
  SPN_TARGET_DS_BIND_PROTO(_p_id, _ds_id)                                                          \
  {                                                                                                \
    vkCmdBindDescriptorSets(cb,                                                                    \
                            VK_PIPELINE_BIND_POINT_COMPUTE,                                        \
                            target->pl.named._p_id,                                                \
                            _ds_idx,                                                               \
                            1,                                                                     \
                            target->dutdp._ds_id.ds + ds.idx,                                      \
                            0,                                                                     \
                            NULL);                                                                 \
  }

#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) _p_descs

SPN_TARGET_P_EXPAND()

//
// Write push constants to command buffer
//

#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs)                                             \
  SPN_TARGET_P_PUSH_PROTO(_p_id)                                                                   \
  {                                                                                                \
    vkCmdPushConstants(cb,                                                                         \
                       target->pl.named._p_id,                                                     \
                       VK_SHADER_STAGE_COMPUTE_BIT,                                                \
                       0,                                                                          \
                       target->config.p.push_sizes.named._p_id,                                    \
                       push);                                                                      \
  }

SPN_TARGET_P_EXPAND()

//
// Bind pipeline set to command buffer
//

#undef SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs)                                             \
  SPN_TARGET_P_BIND_PROTO(_p_id)                                                                   \
  {                                                                                                \
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, target->p.named._p_id);                  \
  }

SPN_TARGET_P_EXPAND()

//
// Most descriptor sets are only acquired immediately before a
// pipeline launch.
//
// For a descriptor set with permanent/durable extents:
//
//   1. allocate permanent/durable host-side and device-side extents
//
// Before launching a compute shader:
//
//   2. acquire a ds from the pool and:
//     a. flush permanent mapped device-side extents from host-side (no-op for local-coherent mem)
//     b. allocate temporary/ephemeral host-side extents (rare!)
//     c. allocate temporary/ephemeral device-side extents
//     d. update the ds with buffers,images,etc.
//
//   3. bind the ds to a command buffer
//
//   4. upon pipeline completion or opportunistically:
//     a. free temporary/ephemeral device-side extents
//     b. free temporary/ephemeral host-side extents
//     c. release the ds back to the pool
//
// Note the block pool descriptor set is the only exception and is
// acquired and allocated once per context and used by most of the
// compute shaders in the Spinel pipeline.
//

//
// spn_extent_alloc_perm(spn,spn_extent * * extent)
//
// spn_extent_alloc_perm(spn,spn_extent * * extent)
//
// spn_extent_free_perm (spn,spn_extent   * extent)
//

//
//
//

void
spn_target_extent_alloc(struct spn_target * const      target,
                        VkDescriptorBufferInfo * const dbi,
                        VkDeviceSize const             size,
                        uint32_t const                 props)
{
  ;
}

void
spn_target_extent_free(struct spn_target * const target, VkDescriptorBufferInfo * const dbi)
{
  ;
}

//
//
//

#if 0
void
spn_target_ds_extents_alloc_block_pool(struct spn_target                         * const target,
                                       struct SPN_TARGET_DUTD_NAME(block_pool) * * const block_pool)
{
  // allocate buffers
#undef SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                         \
  spn_target_extent_alloc(spn,                                                                     \
                          &(*block_pool)->_d_id.entry,                                             \
                          target->config.ds.block_pool.mem.sizes._d_id,                            \
                          target->config.ds.block_pool.mem.props._d_id);

  SPN_TARGET_DS_BLOCK_POOL();
}

void
spn_target_ds_extents_free_block_pool(struct spn_target                       * const target,
                                      struct SPN_TARGET_DUTD_NAME(block_pool) * const block_pool)
{
  // allocate buffers
#undef SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                         \
  spn_target_extent_free(spn, &block_pool->_d_id.entry);

  SPN_TARGET_DS_BLOCK_POOL();
}
#endif

//
//
//
