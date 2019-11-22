// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk.h"

#include <assert.h>  // REMOVE ME once we get DUTD integrated with scheduler
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "trace.h"

//
// AMD_shader_info
//

#ifndef NDEBUG

#if defined(SPN_VK_SHADER_INFO_AMD_STATISTICS) || defined(SPN_VK_SHADER_INFO_AMD_DISASSEMBLY)
#include "common/vk/shader_info_amd.h"
#endif

#endif

//
// Associates a readable name with a pipeline
//

#if !defined(NDEBUG) && defined(SPN_VK_PIPELINE_CODE_SIZE)
#define SPN_VK_PIPELINE_NAMES_DEFINE
#endif

//
//
//

#include "device.h"
#include "vk_target.h"

//
// Verify pipeline count matches
//

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_) +1

STATIC_ASSERT_MACRO_1((0 + SPN_VK_P_EXPAND()) == SPN_VK_P_COUNT);

//
// Verify descriptor set count matches
//

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) +1

STATIC_ASSERT_MACRO_1((0 + SPN_VK_DS_EXPAND()) == SPN_VK_DS_COUNT);

/////////////////////////////////////////////////////////////////
//
// VULKAN DESCRIPTOR SET & PIPELINE EXPANSIONS
//

#define SPN_VK_DS_E(id) spn_vk_ds_idx_##id

typedef enum spn_vk_ds_e
{

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) SPN_VK_DS_E(ds_id_) = ds_idx_,

  SPN_VK_DS_EXPAND()

} spn_vk_ds_e;

//
//
//

struct spn_vk_pipelines
{
#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_) VkPipeline p_id_;

  SPN_VK_P_EXPAND()
};

//
// Define the pipeline names
//

#ifdef SPN_VK_PIPELINE_NAMES_DEFINE

static char const * const spn_vk_p_names[] = {

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_) #p_id_,

  SPN_VK_P_EXPAND()
};

#define SPN_VK_PIPELINE_NAMES spn_vk_p_names

#else

#define SPN_VK_PIPELINE_NAMES NULL

#endif

//
//
//

struct spn_vk_pl
{
#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_) VkPipelineLayout p_id_;

  SPN_VK_P_EXPAND()
};

//
// Create VkDescriptorSetLayoutBinding structs
//

#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_idx_, d_ext_, d_id_)                             \
  { .binding            = d_idx_,                                                                  \
    .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,                                       \
    .descriptorCount    = 1,                                                                       \
    .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,                                             \
    .pImmutableSamplers = NULL },

#undef SPN_VK_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_DESC_TYPE_STORAGE_IMAGE(ds_id_, d_idx_, d_ext_, d_id_)                              \
  { .binding            = d_idx_,                                                                  \
    .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,                                        \
    .descriptorCount    = 1,                                                                       \
    .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,                                             \
    .pImmutableSamplers = NULL },

#define SPN_VK_DSLB_NAME(ds_id_) spn_vk_dslb_##ds_id_

#define SPN_VK_DSLB_CREATE(ds_id_, ...)                                                            \
  static const VkDescriptorSetLayoutBinding SPN_VK_DSLB_NAME(ds_id_)[] = { __VA_ARGS__ };

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ...) SPN_VK_DSLB_CREATE(ds_id_, __VA_ARGS__)

SPN_VK_DS_EXPAND();

//
// Create VkDescriptorSetLayoutCreateInfo structs
//

#define SPN_VK_DSLCI(dslb)                                                                         \
  {                                                                                                \
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = NULL, .flags = 0,       \
    .bindingCount = ARRAY_LENGTH_MACRO(dslb), .pBindings = dslb                                    \
  }

#define SPN_VK_DSLCI_NAME(ds_id_) spn_vk_dslci_##ds_id_

#define SPN_VK_DSLCI_CREATE(ds_id_)                                                                \
  static const VkDescriptorSetLayoutCreateInfo SPN_VK_DSLCI_NAME(                                  \
    ds_id_)[] = { SPN_VK_DSLCI(SPN_VK_DSLB_NAME(ds_id_)) }

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) SPN_VK_DSLCI_CREATE(ds_id_);

SPN_VK_DS_EXPAND()

//
// DSL -- descriptor set layout
//

#define SPN_VK_DSL_DECLARE(ds_id_) VkDescriptorSetLayout ds_id_

struct spn_vk_dsl
{
#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) SPN_VK_DSL_DECLARE(ds_id_);

  SPN_VK_DS_EXPAND()
};

//
// DUTD -- descriptor update template data
//

#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_idx_, d_ext_, d_id_)                             \
  struct                                                                                           \
  {                                                                                                \
    VkDescriptorBufferInfo entry;                                                                  \
  } d_id_;

#undef SPN_VK_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_DESC_TYPE_STORAGE_IMAGE(ds_id_, d_idx_, d_ext_, d_id_)                              \
  struct                                                                                           \
  {                                                                                                \
    VkDescriptorImageInfo entry;                                                                   \
  } d_id_;

#define SPN_VK_DUTD_NAME(ds_id_) spn_vk_dutd_##ds_id_

#define SPN_VK_DUTD_DEFINE(ds_id_, ds_)                                                            \
  struct SPN_VK_DUTD_NAME(ds_id_)                                                                  \
  {                                                                                                \
    ds_                                                                                            \
  };

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) SPN_VK_DUTD_DEFINE(ds_id_, ds_)

SPN_VK_DS_EXPAND()

//
// DUTDP -- DUTD pool
//
//   dutds : array of all DUTD structs
//   pool  : LIFO stack of indices to available DUTD structs
//   rem   : remaining number of DUTD indices in LIFO pool
//   size  : size of dutds array
//

#define SPN_VK_DUTDP_DEFINE(ds_id_)                                                                \
  struct                                                                                           \
  {                                                                                                \
    struct SPN_VK_DUTD_NAME(ds_id_) * dutds;                                                       \
    uint32_t *        pool;                                                                        \
    VkDescriptorSet * ds;                                                                          \
    uint32_t          rem;                                                                         \
    uint32_t          size;                                                                        \
  } ds_id_;

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) SPN_VK_DUTDP_DEFINE(ds_id_)

struct spn_vk_dutdp
{
  SPN_VK_DS_EXPAND()

  void * mem;  // single allocation
};

//
// DUTE -- descriptor update template entry
//

#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_idx_, d_ext_, d_id_)                             \
  { .dstBinding      = d_idx_,                                                                     \
    .dstArrayElement = 0,                                                                          \
    .descriptorCount = 1,                                                                          \
    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,                                          \
    .offset          = offsetof(struct SPN_VK_DUTD_NAME(ds_id_), d_id_),                           \
    .stride          = 0 },

#undef SPN_VK_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_DESC_TYPE_STORAGE_IMAGE(ds_id_, d_idx_, d_ext_, d_id_)                              \
  { .dstBinding      = d_idx_,                                                                     \
    .dstArrayElement = 0,                                                                          \
    .descriptorCount = 1,                                                                          \
    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,                                           \
    .offset          = offsetof(struct SPN_VK_DUTD_NAME(ds_id_), d_id_),                           \
    .stride          = 0 },

#define SPN_VK_DUTE_NAME(ds_id_) spn_vk_dute_##ds_id_

#define SPN_VK_DUTE_CREATE(ds_id_, ...)                                                            \
  static const VkDescriptorUpdateTemplateEntry SPN_VK_DUTE_NAME(ds_id_)[] = { __VA_ARGS__ };

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ...) SPN_VK_DUTE_CREATE(ds_id_, __VA_ARGS__)

SPN_VK_DS_EXPAND()

//
// DUT -- descriptor update template
//

#define SPN_VK_DUT_DECLARE(ds_id_) VkDescriptorUpdateTemplate ds_id_

struct spn_vk_dut
{
#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) SPN_VK_DUT_DECLARE(ds_id_);

  SPN_VK_DS_EXPAND()
};

//
// DP -- define descriptor pools
//

#define SPN_VK_DP_DECLARE(ds_id_) VkDescriptorPool ds_id_

struct spn_vk_dp
{
#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) SPN_VK_DP_DECLARE(ds_id_);

  SPN_VK_DS_EXPAND()
};

//
// DPS -- define descriptor pool size
//

#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_idx_, d_ext_, d_id_)                             \
  { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = SPN_VK_DPS_COUNT(ds_id_) },

#undef SPN_VK_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_DESC_TYPE_STORAGE_IMAGE(ds_id_, d_idx_, d_ext_, d_id_)                              \
  { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = SPN_VK_DPS_COUNT(ds_id_) },

#define SPN_VK_DPS_NAME(ds_id_) spn_vk_dps_##ds_id_

#define SPN_VK_DPS_DEFINE(ds_id_, ...)                                                             \
  VkDescriptorPoolSize const SPN_VK_DPS_NAME(ds_id_)[] = { __VA_ARGS__ };

//
// PDUTD - pipeline descriptor update template types
//

#undef SPN_VK_HOST_DS
#define SPN_VK_HOST_DS(p_id_, ds_idx_, ds_id_) struct SPN_VK_DUTD_NAME(ds_id_) ds_id_;

#undef SPN_VK_HOST_PUSH
#define SPN_VK_HOST_PUSH(p_id_, _p_pc)

#define SPN_VK_PDUTD_NAME(p_id_) spn_vk_pdutd_##p_id_

#define SPN_VK_PDUTD_DEFINE(p_id_, p_descs_)                                                       \
  struct SPN_VK_PDUTD_NAME(p_id_)                                                                  \
  {                                                                                                \
    p_descs_                                                                                       \
  };

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_) SPN_VK_PDUTD_DEFINE(p_id_, p_descs_)

SPN_VK_P_EXPAND()

//
// clang-format off
//

struct spn_vk
{
  struct spn_vk_target_config  config;

  union
  {
    struct spn_vk_dsl          named;  // descriptor set layouts
    VkDescriptorSetLayout      array[SPN_VK_DS_COUNT];
  } dsl;

  union
  {
    struct spn_vk_dut          named;  // descriptor update templates
    VkDescriptorUpdateTemplate array[SPN_VK_DS_COUNT];
  } dut;

  union
  {
    struct spn_vk_dp           named;  // descriptor pools
    VkDescriptorPool           array[SPN_VK_DS_COUNT];
  } dp;

  struct spn_vk_dutdp          dutdp;  // descriptor update template data pools

  union
  {
    struct spn_vk_pl           named;  // pipeline layouts
    VkPipelineLayout           array[SPN_VK_P_COUNT];
  } pl;

  union
  {
    struct spn_vk_pipelines    named;  // pipelines
    VkPipeline                 array[SPN_VK_P_COUNT];
  } p;
};

//
// clang-format on
//

struct spn_vk_target_config const *
spn_vk_get_config(struct spn_vk const * const instance)
{
  return &instance->config;
}

//
//
//

static void
spn_vk_dutdp_free(struct spn_vk * const instance, struct spn_vk_environment * const environment)
{
#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_)                                                   \
  {                                                                                                \
    uint32_t const   size = instance->dutdp.ds_id_.size;                                           \
    VkDescriptorPool dp   = instance->dp.array[ds_idx_];                                           \
                                                                                                   \
    vk(FreeDescriptorSets(environment->d, dp, size, instance->dutdp.ds_id_.ds));                   \
  }

  SPN_VK_DS_EXPAND()

  free(instance->dutdp.mem);
}

//
//
//

void
spn_vk_dispose(struct spn_vk * const instance, struct spn_vk_environment * const environment)
{
  //
  // PIPELINE
  //
  for (uint32_t ii = 0; ii < SPN_VK_P_COUNT; ii++)
    {
      vkDestroyPipeline(environment->d, instance->p.array[ii], environment->ac);
    }

  //
  // PL
  //
  for (uint32_t ii = 0; ii < SPN_VK_P_COUNT; ii++)
    {
      vkDestroyPipelineLayout(environment->d, instance->pl.array[ii], environment->ac);
    }

  //
  // DUTDP
  //
  spn_vk_dutdp_free(instance, environment);

  //
  // DP
  //
  for (uint32_t ii = 0; ii < SPN_VK_DS_COUNT; ii++)
    {
      vkDestroyDescriptorPool(environment->d, instance->dp.array[ii], environment->ac);
    }

  //
  // DUT
  //
  for (uint32_t ii = 0; ii < SPN_VK_DS_COUNT; ii++)
    {
      vkDestroyDescriptorUpdateTemplate(environment->d, instance->dut.array[ii], environment->ac);
    }

  //
  // DSL
  //
  for (uint32_t ii = 0; ii < SPN_VK_DS_COUNT; ii++)
    {
      vkDestroyDescriptorSetLayout(environment->d, instance->dsl.array[ii], environment->ac);
    }

  //
  // SPN
  //
  free(instance);
}

//
//
//

struct spn_vk *
spn_vk_create(struct spn_vk_environment * const  environment,
              struct spn_vk_target const * const target)
{
  //
  // allocate spn_vk
  //
  struct spn_vk * instance = malloc(sizeof(*instance));

  // save config
  memcpy(&instance->config, &target->config, sizeof(target->config));

  //
  // DSL -- create descriptor set layout
  //
#define SPN_VK_DSL_CREATE(id)                                                                      \
  vk(CreateDescriptorSetLayout(environment->d,                                                     \
                               SPN_VK_DSLCI_NAME(id),                                              \
                               environment->ac,                                                    \
                               &instance->dsl.named.id))

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(idx, id, desc) SPN_VK_DSL_CREATE(id);

  SPN_VK_DS_EXPAND();

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
    .set                        = 0
  };

#define SPN_VK_DUT_CREATE(id)                                                                      \
  dutci.descriptorUpdateEntryCount = ARRAY_LENGTH_MACRO(SPN_VK_DUTE_NAME(id));                     \
  dutci.pDescriptorUpdateEntries   = SPN_VK_DUTE_NAME(id);                                         \
  dutci.descriptorSetLayout        = instance->dsl.named.id;                                       \
  vk(CreateDescriptorUpdateTemplate(environment->d,                                                \
                                    &dutci,                                                        \
                                    environment->ac,                                               \
                                    &instance->dut.named.id))

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(idx, id, desc) SPN_VK_DUT_CREATE(id);

  SPN_VK_DS_EXPAND();

  //
  // DP -- create descriptor pools
  //
  VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                      .pNext = NULL,
                                      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT };

#define SPN_VK_DPS_COUNT(ds_id_) instance->config.ds.ds_id_.sets

#define SPN_VK_DP_CREATE(ds_idx_, ds_id_, ...)                                                     \
  {                                                                                                \
    SPN_VK_DPS_DEFINE(ds_id_, __VA_ARGS__);                                                        \
                                                                                                   \
    dpci.maxSets       = instance->config.ds.ds_id_.sets;                                          \
    dpci.poolSizeCount = ARRAY_LENGTH_MACRO(SPN_VK_DPS_NAME(ds_id_));                              \
    dpci.pPoolSizes    = SPN_VK_DPS_NAME(ds_id_);                                                  \
    vk(CreateDescriptorPool(environment->d, &dpci, environment->ac, &instance->dp.named.ds_id_));  \
  }

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ...) SPN_VK_DP_CREATE(ds_idx_, ds_id_, __VA_ARGS__);

  SPN_VK_DS_EXPAND();

  //
  // DUTD POOLS
  //
  size_t dutd_total = 0;

  // array of pointers + array of structs
#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_)                                                   \
  instance->dutdp.ds_id_.rem  = instance->config.ds.ds_id_.sets;                                   \
  instance->dutdp.ds_id_.size = instance->config.ds.ds_id_.sets;                                   \
  dutd_total += instance->config.ds.ds_id_.sets *                                                  \
                (sizeof(*instance->dutdp.ds_id_.dutds) + sizeof(*instance->dutdp.ds_id_.pool) +    \
                 sizeof(*instance->dutdp.ds_id_.ds));

  SPN_VK_DS_EXPAND();

  void * dutd_base = malloc(dutd_total);

  // save the memory blob
  instance->dutdp.mem = dutd_base;

  VkDescriptorSetAllocateInfo dsai = {
    .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext              = NULL,
    .descriptorSetCount = 1,
    // .descriptorPool  = instance->dp.array[idx],
    // .pSetLayouts     = instance->dsl.array + idx
  };

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_)                                                    \
  {                                                                                                 \
    uint32_t const size = instance->dutdp.ds_id_.size;                                              \
                                                                                                    \
    instance->dutdp.ds_id_.dutds = dutd_base;                                                       \
    dutd_base = ((uint8_t *)dutd_base + sizeof(*instance->dutdp.ds_id_.dutds) * size);              \
                                                                                                    \
    instance->dutdp.ds_id_.pool = dutd_base;                                                        \
    dutd_base = ((uint8_t *)dutd_base + sizeof(*instance->dutdp.ds_id_.pool) * size);               \
                                                                                                    \
    instance->dutdp.ds_id_.ds = dutd_base;                                                          \
    dutd_base                 = ((uint8_t *)dutd_base + sizeof(*instance->dutdp.ds_id_.ds) * size); \
                                                                                                    \
    dsai.descriptorPool = instance->dp.array[ds_idx_];                                              \
    dsai.pSetLayouts    = instance->dsl.array + ds_idx_;                                            \
                                                                                                    \
    for (uint32_t ii = 0; ii < size; ii++)                                                          \
      {                                                                                             \
        instance->dutdp.ds_id_.pool[ii] = ii;                                                       \
                                                                                                    \
        vkAllocateDescriptorSets(environment->d, &dsai, instance->dutdp.ds_id_.ds + ii);            \
      }                                                                                             \
                                                                                                    \
    SPN_VK_TRACE_DS_POOL_CREATE(STRINGIFY_MACRO(ds_id_), size);                                     \
  }

  SPN_VK_DS_EXPAND();

  //
  // PL -- create pipeline layouts
  //
  VkPushConstantRange pcr[] = {
    { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 0 }
  };

  VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .pNext = NULL,
                                      .flags = 0,
                                      .setLayoutCount         = 0,
                                      .pSetLayouts            = NULL,
                                      .pushConstantRangeCount = 0,
                                      .pPushConstantRanges    = NULL };

#undef SPN_VK_HOST_DS
#define SPN_VK_HOST_DS(p_id_, ds_idx_, ds_id_) instance->dsl.named.ds_id_,

#define SPN_VK_PL_CREATE(p_idx_, p_id_, ...)                                                       \
  {                                                                                                \
    uint32_t const pps = instance->config.p.push_sizes.array[p_idx_];                              \
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
    const VkDescriptorSetLayout dsls[] = { __VA_ARGS__ };                                          \
                                                                                                   \
    plci.setLayoutCount = ARRAY_LENGTH_MACRO(dsls);                                                \
    plci.pSetLayouts    = dsls;                                                                    \
                                                                                                   \
    vk(CreatePipelineLayout(environment->d, &plci, environment->ac, &instance->pl.named.p_id_));   \
  }

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, ...) SPN_VK_PL_CREATE(p_idx_, p_id_, __VA_ARGS__)

  SPN_VK_P_EXPAND();

  //
  // Prepare to create compute pipelines
  //
  VkComputePipelineCreateInfo cpci = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .stage = { .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .pNext               = NULL,
               .flags               = 0,
               .stage               = VK_SHADER_STAGE_COMPUTE_BIT,
               .module              = VK_NULL_HANDLE,
               .pName               = "main",
               .pSpecializationInfo = NULL },

    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex  = 0
  };

  //
  // Set the subgroup size to what we expected when we built the
  // Spinel target
  //
  // FIXME(allanmac): remove this as soon as we update Fuchsia's toolchain
  //
#ifndef VK_EXT_subgroup_size_control

#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT 1000225001
#define VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT 0x00000002

  typedef struct VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT
  {
    VkStructureType sType;
    void *          pNext;
    uint32_t        requiredSubgroupSize;
  } VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT;

#endif
  //
  // REMOVEME(allanmac) ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  //

  //
  // Control the pipeline's subgroup size
  //
  VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT rssci = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
    .pNext = NULL,
    .requiredSubgroupSize = 0
  };

  //
  // For each shader module, create a pipeline... and then dispose of the shader module.
  //
  VkShaderModuleCreateInfo smci = { .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                    .pNext    = NULL,
                                    .flags    = 0,
                                    .codeSize = 0,
                                    .pCode    = NULL };

  uint32_t const * modules = target->modules;

  for (uint32_t ii = 0; ii < SPN_VK_P_COUNT; ii++)
    {
      uint32_t const module_dwords = *modules++;

      smci.codeSize = module_dwords * sizeof(*modules);
      smci.pCode    = modules;

      modules += module_dwords;

      //
      // DEBUG
      //
#if !defined(NDEBUG) && defined(SPN_VK_PIPELINE_CODE_SIZE)
      fprintf(stderr, "%-38s ", SPN_VK_PIPELINE_NAMES[ii]);
      fprintf(stderr, "(codeSize = %6zu) ... ", smci.codeSize);
#endif

      //
      // is subgroup size control active?
      //
      if (target->config.extensions.named.EXT_subgroup_size_control)
        {
          rssci.requiredSubgroupSize = 1 << instance->config.p.group_sizes.array[ii].subgroup_log2;

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

      vk(CreateShaderModule(environment->d, &smci, environment->ac, &cpci.stage.module));

      cpci.layout = instance->pl.array[ii];

      vk(CreateComputePipelines(environment->d,
                                environment->pc,
                                1,
                                &cpci,
                                environment->ac,
                                instance->p.array + ii));

      vkDestroyShaderModule(environment->d, cpci.stage.module, environment->ac);

      //
      // DEBUG
      //
#if !defined(NDEBUG) && defined(SPN_VK_PIPELINE_CODE_SIZE)
      fprintf(stderr, "OK\n");
#endif
    }

    //
    // optionally dump pipeline stats on AMD devices
    //
#ifndef NDEBUG

#ifdef SPN_VK_SHADER_INFO_AMD_STATISTICS
  if (instance->config.extensions.named.AMD_shader_info)
    {
      vk_shader_info_amd_statistics(environment->d,
                                    instance->p.array,
                                    SPN_VK_PIPELINE_NAMES,
                                    SPN_VK_P_COUNT);
    }
#endif
#ifdef SPN_VK_SHADER_INFO_AMD_DISASSEMBLY
  if (instance->config.extensions.named.AMD_shader_info)
    {
      vk_shader_info_amd_disassembly(environment->d,
                                     instance->p.array,
                                     SPN_VK_PIPELINE_NAMES,
                                     SPN_VK_P_COUNT);
    }
#endif

#endif

  //
  // Done...
  //
  return instance;
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

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_)                                                   \
  SPN_VK_DS_ACQUIRE_PROTO(ds_id_)                                                                  \
  {                                                                                                \
    while (instance->dutdp.ds_id_.rem == 0)                                                        \
      {                                                                                            \
        SPN_DEVICE_WAIT(device);                                                                   \
      }                                                                                            \
                                                                                                   \
    ds->idx = instance->dutdp.ds_id_.pool[--instance->dutdp.ds_id_.rem];                           \
                                                                                                   \
    SPN_VK_TRACE_DS_POOL_ACQUIRE(STRINGIFY_MACRO(ds_id_), ds->idx);                                \
  }                                                                                                \
                                                                                                   \
  SPN_VK_DS_RELEASE_PROTO(ds_id_)                                                                  \
  {                                                                                                \
    instance->dutdp.ds_id_.pool[instance->dutdp.ds_id_.rem++] = ds.idx;                            \
                                                                                                   \
    SPN_VK_TRACE_DS_POOL_ACQUIRE(STRINGIFY_MACRO(ds_id_), ds->idx);                                \
  }

SPN_VK_DS_EXPAND()

//
// Get reference to descriptor update template
//

#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_idx_, d_ext_, d_id_)                             \
  SPN_VK_DS_GET_PROTO_STORAGE_BUFFER(ds_id_, d_id_)                                                \
  {                                                                                                \
    return &instance->dutdp.ds_id_.dutds[ds.idx].d_id_.entry;                                      \
  }

#undef SPN_VK_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_DESC_TYPE_STORAGE_IMAGE(ds_id_, d_idx_, d_ext_, d_id_)                              \
  SPN_VK_DS_GET_PROTO_STORAGE_IMAGE(ds_id_, d_id_)                                                 \
  {                                                                                                \
    return &instance->dutdp.ds_id_.dutds[ds.idx].d_id_.entry;                                      \
  }

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) ds_

SPN_VK_DS_EXPAND()

//
// Update the descriptor set
//

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_)                                                   \
  SPN_VK_DS_UPDATE_PROTO(ds_id_)                                                                   \
  {                                                                                                \
    vkUpdateDescriptorSetWithTemplate(environment->d,                                              \
                                      instance->dutdp.ds_id_.ds[ds.idx],                           \
                                      instance->dut.named.ds_id_,                                  \
                                      instance->dutdp.ds_id_.dutds + ds.idx);                      \
  }

SPN_VK_DS_EXPAND()

//
// Bind descriptor set to command buffer
//

#undef SPN_VK_HOST_DS
#define SPN_VK_HOST_DS(p_id_, ds_idx_, ds_id_)                                                     \
  SPN_VK_DS_BIND_PROTO(p_id_, ds_id_)                                                              \
  {                                                                                                \
    vkCmdBindDescriptorSets(cb,                                                                    \
                            VK_PIPELINE_BIND_POINT_COMPUTE,                                        \
                            instance->pl.named.p_id_,                                              \
                            ds_idx_,                                                               \
                            1,                                                                     \
                            instance->dutdp.ds_id_.ds + ds.idx,                                    \
                            0,                                                                     \
                            NULL);                                                                 \
  }

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_) p_descs_

SPN_VK_P_EXPAND()

//
// Write push constants to command buffer
//

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_)                                                 \
  SPN_VK_P_PUSH_PROTO(p_id_)                                                                       \
  {                                                                                                \
    vkCmdPushConstants(cb,                                                                         \
                       instance->pl.named.p_id_,                                                   \
                       VK_SHADER_STAGE_COMPUTE_BIT,                                                \
                       0,                                                                          \
                       instance->config.p.push_sizes.named.p_id_,                                  \
                       push);                                                                      \
  }

SPN_VK_P_EXPAND()

//
// Bind pipeline set to command buffer
//

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_idx_, p_id_, p_descs_)                                                 \
  SPN_VK_P_BIND_PROTO(p_id_)                                                                       \
  {                                                                                                \
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, instance->p.named.p_id_);                \
  }

SPN_VK_P_EXPAND()

//
// Get the VkPipelineLayout that HotSort will operate on
//

VkPipelineLayout
spn_vk_pl_hotsort(struct spn_vk const * const instance)
{
  // Both SPN_VK_P_ID_SEGMENT_TTRK and SPN_VK_P_ID_SEGMENT_TTCK
  // pipelines have compatible pipeline layouts
  return instance->pl.named.SPN_VK_P_ID_SEGMENT_TTCK;
}

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
// spn_extent_alloc_perm(instance,spn_extent * * extent)
//
// spn_extent_alloc_perm(instance,spn_extent * * extent)
//
// spn_extent_free_perm (instance,spn_extent   * extent)
//

//
//
//

void
spn_vk_extent_alloc(struct spn_vk * const          instance,
                    VkDescriptorBufferInfo * const dbi,
                    VkDeviceSize const             size,
                    uint32_t const                 props)
{
  ;
}

void
spn_vk_extent_free(struct spn_vk * const instance, VkDescriptorBufferInfo * const dbi)
{
  ;
}

//
//
//

#if 0  // TODO: DISABLED UNTIL IN USE
void
spn_vk_ds_extents_alloc_block_pool(struct spn_vk * const                         instance,
                                   struct SPN_VK_DUTD_NAME(block_pool) * * const block_pool)
{
  // allocate buffers
#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_idx_, d_ext_, d_id_)                             \
  spn_vk_extent_alloc(instance,                                                                    \
                      &(*block_pool)->d_id_.entry,                                                 \
                      instance->config.ds.block_pool.mem.sizes.d_id_,                              \
                      instance->config.ds.block_pool.mem.props.d_id_);

  SPN_VK_DS_BLOCK_POOL();
}

void
spn_vk_ds_extents_free_block_pool(struct spn_vk * const                       instance,
                                  struct SPN_VK_DUTD_NAME(block_pool) * const block_pool)
{
  // allocate buffers
#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_idx_, d_ext_, d_id_)                             \
  spn_vk_extent_free(instance, &block_pool->d_id_.entry);

  SPN_VK_DS_BLOCK_POOL();
}
#endif

//
//
//
