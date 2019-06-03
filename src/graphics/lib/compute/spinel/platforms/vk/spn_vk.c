// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spn_vk.h"

#include <assert.h>  // REMOVE ME once we get DUTD integrated with scheduler
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/macros.h"
#include "common/vk/vk_assert.h"
#include "device.h"
#include "spn_vk_target.h"

//
// Verify pipeline count matches
//

#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) +1

STATIC_ASSERT_MACRO_1((0 + SPN_VK_TARGET_P_EXPAND()) == SPN_VK_TARGET_P_COUNT);

//
// Verify descriptor set count matches
//

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) +1

STATIC_ASSERT_MACRO_1((0 + SPN_VK_TARGET_DS_EXPAND()) == SPN_VK_TARGET_DS_COUNT);

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

#define SPN_VK_TARGET_DS_E(id) spn_vk_ds_idx_##id

typedef enum spn_vk_ds_e
{

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_VK_TARGET_DS_E(_ds_id) = _ds_idx,

  SPN_VK_TARGET_DS_EXPAND()

} spn_vk_ds_e;

//
//
//

struct spn_vk_pipelines
{
#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) VkPipeline _p_id;

  SPN_VK_TARGET_P_EXPAND()
};

//
//
//

#define SPN_VK_TARGET_PIPELINE_NAMES_DEFINE

#ifdef SPN_VK_TARGET_PIPELINE_NAMES_DEFINE

static char const * const spn_vk_p_names[] = {
#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) #_p_id,

  SPN_VK_TARGET_P_EXPAND()
};

#define SPN_VK_TARGET_PIPELINE_NAMES spn_vk_p_names

#else

#define SPN_VK_TARGET_PIPELINE_NAMES NULL

#endif

//
//
//

struct spn_vk_pl
{
#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) VkPipelineLayout _p_id;

  SPN_VK_TARGET_P_EXPAND()
};

//
// Create VkDescriptorSetLayoutBinding structs
//

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                      \
  { .binding            = _d_idx,                                                                  \
    .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,                                       \
    .descriptorCount    = 1,                                                                       \
    .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,                                             \
    .pImmutableSamplers = NULL },

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                       \
  { .binding            = _d_idx,                                                                  \
    .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,                                        \
    .descriptorCount    = 1,                                                                       \
    .stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT,                                             \
    .pImmutableSamplers = NULL },

#define SPN_VK_TARGET_DSLB_NAME(_ds_id) spn_vk_dslb_##_ds_id

#define SPN_VK_TARGET_DSLB_CREATE(_ds_id, ...)                                                     \
  static const VkDescriptorSetLayoutBinding SPN_VK_TARGET_DSLB_NAME(_ds_id)[] = { __VA_ARGS__ };

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, ...)                                            \
  SPN_VK_TARGET_DSLB_CREATE(_ds_id, __VA_ARGS__)

SPN_VK_TARGET_DS_EXPAND();

//
// Create VkDescriptorSetLayoutCreateInfo structs
//

#define SPN_VK_TARGET_DSLCI(dslb)                                                                  \
  {                                                                                                \
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = NULL, .flags = 0,       \
    .bindingCount = ARRAY_LENGTH_MACRO(dslb), .pBindings = dslb                                    \
  }

#define SPN_VK_TARGET_DSLCI_NAME(_ds_id) spn_vk_dslci_##_ds_id

#define SPN_VK_TARGET_DSLCI_CREATE(_ds_id)                                                         \
  static const VkDescriptorSetLayoutCreateInfo SPN_VK_TARGET_DSLCI_NAME(                           \
    _ds_id)[] = { SPN_VK_TARGET_DSLCI(SPN_VK_TARGET_DSLB_NAME(_ds_id)) }

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_VK_TARGET_DSLCI_CREATE(_ds_id);

SPN_VK_TARGET_DS_EXPAND()

//
// DSL -- descriptor set layout
//

#define SPN_VK_TARGET_DSL_DECLARE(_ds_id) VkDescriptorSetLayout _ds_id

struct spn_vk_dsl
{
#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_VK_TARGET_DSL_DECLARE(_ds_id);

  SPN_VK_TARGET_DS_EXPAND()
};

//
// DUTD -- descriptor update template data
//

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                      \
  struct                                                                                           \
  {                                                                                                \
    VkDescriptorBufferInfo entry;                                                                  \
  } _d_id;

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                       \
  struct                                                                                           \
  {                                                                                                \
    VkDescriptorImageInfo entry;                                                                   \
  } _d_id;

#define SPN_VK_TARGET_DUTD_NAME(_ds_id) spn_vk_dutd_##_ds_id

#define SPN_VK_TARGET_DUTD_DEFINE(_ds_id, _ds)                                                     \
  struct SPN_VK_TARGET_DUTD_NAME(_ds_id)                                                           \
  {                                                                                                \
    _ds                                                                                            \
  };

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_VK_TARGET_DUTD_DEFINE(_ds_id, _ds)

SPN_VK_TARGET_DS_EXPAND()

//
// DUTDP -- DUTD pool
//
//   dutds : array of all DUTD structs
//   pool  : LIFO stack of indices to available DUTD structs
//   rem   : remaining number of DUTD indices in LIFO pool
//   size  : size of dutds array
//

#define SPN_VK_TARGET_DUTDP_DEFINE(_ds_id)                                                         \
  struct                                                                                           \
  {                                                                                                \
    struct SPN_VK_TARGET_DUTD_NAME(_ds_id) * dutds;                                                \
    uint32_t *        pool;                                                                        \
    VkDescriptorSet * ds;                                                                          \
    uint32_t          rem;                                                                         \
    uint32_t          size;                                                                        \
  } _ds_id;

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_VK_TARGET_DUTDP_DEFINE(_ds_id)

struct spn_vk_dutdp
{
  SPN_VK_TARGET_DS_EXPAND()

  void * mem;  // single allocation
};

//
// DUTE -- descriptor update template entry
//

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                      \
  { .dstBinding      = _d_idx,                                                                     \
    .dstArrayElement = 0,                                                                          \
    .descriptorCount = 1,                                                                          \
    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,                                          \
    .offset          = offsetof(struct SPN_VK_TARGET_DUTD_NAME(_ds_id), _d_id),                    \
    .stride          = 0 },

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                       \
  { .dstBinding      = _d_idx,                                                                     \
    .dstArrayElement = 0,                                                                          \
    .descriptorCount = 1,                                                                          \
    .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,                                           \
    .offset          = offsetof(struct SPN_VK_TARGET_DUTD_NAME(_ds_id), _d_id),                    \
    .stride          = 0 },

#define SPN_VK_TARGET_DUTE_NAME(_ds_id) spn_vk_dute_##_ds_id

#define SPN_VK_TARGET_DUTE_CREATE(_ds_id, ...)                                                     \
  static const VkDescriptorUpdateTemplateEntry SPN_VK_TARGET_DUTE_NAME(_ds_id)[] = { __VA_ARGS__ };

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, ...)                                            \
  SPN_VK_TARGET_DUTE_CREATE(_ds_id, __VA_ARGS__)

SPN_VK_TARGET_DS_EXPAND()

//
// DUT -- descriptor update template
//

#define SPN_VK_TARGET_DUT_DECLARE(_ds_id) VkDescriptorUpdateTemplate _ds_id

struct spn_vk_dut
{
#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_VK_TARGET_DUT_DECLARE(_ds_id);

  SPN_VK_TARGET_DS_EXPAND()
};

//
// DP -- define descriptor pools
//

#define SPN_VK_TARGET_DP_DECLARE(_ds_id) VkDescriptorPool _ds_id

struct spn_vk_dp
{
#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) SPN_VK_TARGET_DP_DECLARE(_ds_id);

  SPN_VK_TARGET_DS_EXPAND()
};

//
// DPS -- define descriptor pool size
//

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                      \
  { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = SPN_VK_TARGET_DPS_COUNT(_ds_id) },

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                       \
  { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = SPN_VK_TARGET_DPS_COUNT(_ds_id) },

#define SPN_VK_TARGET_DPS_NAME(_ds_id) spn_vk_dps_##_ds_id

#define SPN_VK_TARGET_DPS_DEFINE(_ds_id, ...)                                                      \
  VkDescriptorPoolSize const SPN_VK_TARGET_DPS_NAME(_ds_id)[] = { __VA_ARGS__ };

//
// PDUTD - pipeline descriptor update template types
//

#undef SPN_VK_TARGET_VK_DS
#define SPN_VK_TARGET_VK_DS(_p_id, _ds_idx, _ds_id) struct SPN_VK_TARGET_DUTD_NAME(_ds_id) _ds_id;

#undef SPN_VK_TARGET_VK_PUSH
#define SPN_VK_TARGET_VK_PUSH(_p_id, _p_pc)

#define SPN_VK_TARGET_PDUTD_NAME(_p_id) spn_vk_pdutd_##_p_id

#define SPN_VK_TARGET_PDUTD_DEFINE(_p_id, _p_descs)                                                \
  struct SPN_VK_TARGET_PDUTD_NAME(_p_id)                                                           \
  {                                                                                                \
    _p_descs                                                                                       \
  };

#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs)                                          \
  SPN_VK_TARGET_PDUTD_DEFINE(_p_id, _p_descs)

SPN_VK_TARGET_P_EXPAND()

//
// clang-format off
//

struct spn_vk
{
  struct spn_vk_target_config  config;

  union
  {
    struct spn_vk_dsl          named;  // descriptor set layouts
    VkDescriptorSetLayout      array[SPN_VK_TARGET_DS_COUNT];
  } dsl;

  union
  {
    struct spn_vk_dut          named;  // descriptor update templates
    VkDescriptorUpdateTemplate array[SPN_VK_TARGET_DS_COUNT];
  } dut;

  union
  {
    struct spn_vk_dp           named;  // descriptor pools
    VkDescriptorPool           array[SPN_VK_TARGET_DS_COUNT];
  } dp;

  struct spn_vk_dutdp          dutdp;  // descriptor update template data pools

  union
  {
    struct spn_vk_pl           named;  // pipeline layouts
    VkPipelineLayout           array[SPN_VK_TARGET_P_COUNT];
  } pl;

  union
  {
    struct spn_vk_pipelines    named;  // pipelines
    VkPipeline                 array[SPN_VK_TARGET_P_COUNT];
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
#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                            \
  {                                                                                                \
    uint32_t const   size = instance->dutdp._ds_id.size;                                           \
    VkDescriptorPool dp   = instance->dp.array[_ds_idx];                                           \
                                                                                                   \
    vk(FreeDescriptorSets(environment->d, dp, size, instance->dutdp._ds_id.ds));                   \
  }

  SPN_VK_TARGET_DS_EXPAND()

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
  for (uint32_t ii = 0; ii < SPN_VK_TARGET_P_COUNT; ii++)
    {
      vkDestroyPipeline(environment->d, instance->p.array[ii], environment->ac);
    }

  //
  // PL
  //
  for (uint32_t ii = 0; ii < SPN_VK_TARGET_P_COUNT; ii++)
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
  for (uint32_t ii = 0; ii < SPN_VK_TARGET_DS_COUNT; ii++)
    {
      vkDestroyDescriptorPool(environment->d, instance->dp.array[ii], environment->ac);
    }

  //
  // DUT
  //
  for (uint32_t ii = 0; ii < SPN_VK_TARGET_DS_COUNT; ii++)
    {
      vkDestroyDescriptorUpdateTemplate(environment->d, instance->dut.array[ii], environment->ac);
    }

  //
  // DSL
  //
  for (uint32_t ii = 0; ii < SPN_VK_TARGET_DS_COUNT; ii++)
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
#define SPN_VK_TARGET_DSL_CREATE(id)                                                               \
  vk(CreateDescriptorSetLayout(environment->d,                                                     \
                               SPN_VK_TARGET_DSLCI_NAME(id),                                       \
                               environment->ac,                                                    \
                               &instance->dsl.named.id))

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(idx, id, desc) SPN_VK_TARGET_DSL_CREATE(id);

  SPN_VK_TARGET_DS_EXPAND();

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

#define SPN_VK_TARGET_DUT_CREATE(id)                                                               \
  dutci.descriptorUpdateEntryCount = ARRAY_LENGTH_MACRO(SPN_VK_TARGET_DUTE_NAME(id));              \
  dutci.pDescriptorUpdateEntries   = SPN_VK_TARGET_DUTE_NAME(id);                                  \
  dutci.descriptorSetLayout        = instance->dsl.named.id;                                       \
  vk(CreateDescriptorUpdateTemplate(environment->d,                                                \
                                    &dutci,                                                        \
                                    environment->ac,                                               \
                                    &instance->dut.named.id))

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(idx, id, desc) SPN_VK_TARGET_DUT_CREATE(id);

  SPN_VK_TARGET_DS_EXPAND();

  //
  // DP -- create descriptor pools
  //
  VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                      .pNext = NULL,
                                      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT };

#define SPN_VK_TARGET_DPS_COUNT(_ds_id) instance->config.ds._ds_id.sets

#define SPN_VK_TARGET_DP_CREATE(_ds_idx, _ds_id, ...)                                              \
  {                                                                                                \
    SPN_VK_TARGET_DPS_DEFINE(_ds_id, __VA_ARGS__);                                                 \
                                                                                                   \
    dpci.maxSets       = instance->config.ds._ds_id.sets;                                          \
    dpci.poolSizeCount = ARRAY_LENGTH_MACRO(SPN_VK_TARGET_DPS_NAME(_ds_id));                       \
    dpci.pPoolSizes    = SPN_VK_TARGET_DPS_NAME(_ds_id);                                           \
    vk(CreateDescriptorPool(environment->d, &dpci, environment->ac, &instance->dp.named._ds_id));  \
  }

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, ...)                                            \
  SPN_VK_TARGET_DP_CREATE(_ds_idx, _ds_id, __VA_ARGS__);

  SPN_VK_TARGET_DS_EXPAND();

  //
  // DUTD POOLS
  //
  size_t dutd_total = 0;

  // array of pointers + array of structs
#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                            \
  instance->dutdp._ds_id.rem  = instance->config.ds._ds_id.sets;                                   \
  instance->dutdp._ds_id.size = instance->config.ds._ds_id.sets;                                   \
  dutd_total += instance->config.ds._ds_id.sets *                                                  \
                (sizeof(*instance->dutdp._ds_id.dutds) + sizeof(*instance->dutdp._ds_id.pool) +    \
                 sizeof(*instance->dutdp._ds_id.ds));

  SPN_VK_TARGET_DS_EXPAND();

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

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                             \
  {                                                                                                 \
    uint32_t const size = instance->dutdp._ds_id.size;                                              \
                                                                                                    \
    instance->dutdp._ds_id.dutds = dutd_base;                                                       \
    dutd_base = ((uint8_t *)dutd_base + sizeof(*instance->dutdp._ds_id.dutds) * size);              \
                                                                                                    \
    instance->dutdp._ds_id.pool = dutd_base;                                                        \
    dutd_base = ((uint8_t *)dutd_base + sizeof(*instance->dutdp._ds_id.pool) * size);               \
                                                                                                    \
    instance->dutdp._ds_id.ds = dutd_base;                                                          \
    dutd_base                 = ((uint8_t *)dutd_base + sizeof(*instance->dutdp._ds_id.ds) * size); \
                                                                                                    \
    dsai.descriptorPool = instance->dp.array[_ds_idx];                                              \
    dsai.pSetLayouts    = instance->dsl.array + _ds_idx;                                            \
                                                                                                    \
    for (uint32_t ii = 0; ii < size; ii++)                                                          \
      {                                                                                             \
        instance->dutdp._ds_id.pool[ii] = ii;                                                       \
                                                                                                    \
        vkAllocateDescriptorSets(environment->d, &dsai, instance->dutdp._ds_id.ds + ii);            \
      }                                                                                             \
  }

  SPN_VK_TARGET_DS_EXPAND();

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

#if defined(__Fuchsia__) && defined(__aarch64__)
  //
  // TEMPORARILY FOR FUCHSIA/ARM TARGETS -- only enable RENDER kernel
  //
  bool const p_ok[SPN_VK_TARGET_P_COUNT] = { [16] = true };

#else
  //
  // DEFAULT
  //
#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(...) true,
  bool const p_ok[SPN_VK_TARGET_P_COUNT] = { SPN_VK_TARGET_P_EXPAND() };

#endif

#ifdef SPN_VK_TARGET_PIPELINE_NAMES_DEFINE
#define SPN_PLCI_DEBUG(_p_idx)                                                                     \
  fprintf(stdout,                                                                                  \
          "plci.setLayoutCount[%-38s] = %2u  (%s)\n",                                              \
          SPN_VK_TARGET_PIPELINE_NAMES[_p_idx],                                                    \
          plci.setLayoutCount,                                                                     \
          p_ok[_p_idx] ? "OK" : "SKIP!")
#else
#define SPN_PLCI_DEBUG(_p_idx)
#endif

#undef SPN_VK_TARGET_VK_DS
#define SPN_VK_TARGET_VK_DS(_p_id, _ds_idx, _ds_id) instance->dsl.named._ds_id,

#define SPN_VK_TARGET_PL_CREATE(_p_idx, _p_id, ...)                                                \
  {                                                                                                \
    uint32_t const pps = instance->config.p.push_sizes.array[_p_idx];                              \
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
    SPN_PLCI_DEBUG(_p_idx);                                                                        \
                                                                                                   \
    if (p_ok[_p_idx])                                                                              \
      {                                                                                            \
        vk(CreatePipelineLayout(environment->d,                                                    \
                                &plci,                                                             \
                                environment->ac,                                                   \
                                &instance->pl.named._p_id));                                       \
      }                                                                                            \
  }

#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, ...)                                               \
  SPN_VK_TARGET_PL_CREATE(_p_idx, _p_id, __VA_ARGS__)

  SPN_VK_TARGET_P_EXPAND();

  //
  // create all the compute pipelines by reusing this info
  //
  VkComputePipelineCreateInfo cpci = {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .pNext = NULL,
    .flags = VK_PIPELINE_CREATE_DISPATCH_BASE,  // | VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT,
    .stage = { .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .pNext               = NULL,
               .flags               = 0,
               .stage               = VK_SHADER_STAGE_COMPUTE_BIT,
               .module              = VK_NULL_HANDLE,
               .pName               = "main",
               .pSpecializationInfo = NULL },
    // .layout             = VK_NULL_HANDLE, // instance->pl.layout.vout_vin,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex  = 0
  };

  //
  // Create a shader module, use it to create a pipeline... and
  // dispose of the shader module.
  //
  VkShaderModuleCreateInfo smci = { .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                    .pNext    = NULL,
                                    .flags    = 0,
                                    .codeSize = 0,
                                    .pCode    = NULL };

  uint32_t const * modules = target->modules;

  for (uint32_t ii = 0; ii < SPN_VK_TARGET_P_COUNT; ii++)
    {
      uint32_t const module_dwords = *modules++;

      smci.codeSize = module_dwords * sizeof(*modules);
      smci.pCode    = modules;

      modules += module_dwords;

      fprintf(stdout, "%-38s ", SPN_VK_TARGET_PIPELINE_NAMES[ii]);

      fprintf(stdout, "(codeSize = %6zu) ... ", smci.codeSize);

      if (!p_ok[ii])
        {
          fprintf(stdout, "SKIP!\n");
          continue;
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

      fprintf(stdout, "OK\n");
    }

    //
    // optionally dump pipeline stats on AMD devices
    //
#ifdef SPN_VK_TARGET_SHADER_INFO_AMD_STATISTICS
  vk_shader_info_amd_statistics(environment->d,
                                instance->p.array,
                                SPN_VK_TARGET_PIPELINE_NAMES,
                                SPN_VK_TARGET_P_COUNT);
#endif
#ifdef SPN_VK_TARGET_SHADER_INFO_AMD_DISASSEMBLY
  vk_shader_info_amd_disassembly(environment->d,
                                 instance->p.array,
                                 SPN_VK_TARGET_PIPELINE_NAMES,
                                 SPN_VK_TARGET_P_COUNT);
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

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                            \
  SPN_VK_TARGET_DS_ACQUIRE_PROTO(_ds_id)                                                           \
  {                                                                                                \
    while (instance->dutdp._ds_id.rem == 0)                                                        \
      {                                                                                            \
        spn_device_wait(device);                                                                   \
      }                                                                                            \
    ds->idx = instance->dutdp._ds_id.pool[--instance->dutdp._ds_id.rem];                           \
  }                                                                                                \
                                                                                                   \
  SPN_VK_TARGET_DS_RELEASE_PROTO(_ds_id)                                                           \
  {                                                                                                \
    instance->dutdp._ds_id.pool[instance->dutdp._ds_id.rem++] = ds.idx;                            \
  }

SPN_VK_TARGET_DS_EXPAND()

//
// Get reference to descriptor update template
//

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                      \
  SPN_VK_TARGET_DS_GET_PROTO_STORAGE_BUFFER(_ds_id, _d_id)                                         \
  {                                                                                                \
    return &instance->dutdp._ds_id.dutds[ds.idx]._d_id.entry;                                      \
  }

#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                       \
  SPN_VK_TARGET_DS_GET_PROTO_STORAGE_IMAGE(_ds_id, _d_id)                                          \
  {                                                                                                \
    return &instance->dutdp._ds_id.dutds[ds.idx]._d_id.entry;                                      \
  }

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds) _ds

SPN_VK_TARGET_DS_EXPAND()

//
// Update the descriptor set
//

#undef SPN_VK_TARGET_DS_EXPAND_X
#define SPN_VK_TARGET_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                            \
  SPN_VK_TARGET_DS_UPDATE_PROTO(_ds_id)                                                            \
  {                                                                                                \
    vkUpdateDescriptorSetWithTemplate(environment->d,                                              \
                                      instance->dutdp._ds_id.ds[ds.idx],                           \
                                      instance->dut.named._ds_id,                                  \
                                      instance->dutdp._ds_id.dutds + ds.idx);                      \
  }

SPN_VK_TARGET_DS_EXPAND()

//
// Bind descriptor set to command buffer
//

#undef SPN_VK_TARGET_VK_DS
#define SPN_VK_TARGET_VK_DS(_p_id, _ds_idx, _ds_id)                                                \
  SPN_VK_TARGET_DS_BIND_PROTO(_p_id, _ds_id)                                                       \
  {                                                                                                \
    vkCmdBindDescriptorSets(cb,                                                                    \
                            VK_PIPELINE_BIND_POINT_COMPUTE,                                        \
                            instance->pl.named._p_id,                                              \
                            _ds_idx,                                                               \
                            1,                                                                     \
                            instance->dutdp._ds_id.ds + ds.idx,                                    \
                            0,                                                                     \
                            NULL);                                                                 \
  }

#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs) _p_descs

SPN_VK_TARGET_P_EXPAND()

//
// Write push constants to command buffer
//

#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs)                                          \
  SPN_VK_TARGET_P_PUSH_PROTO(_p_id)                                                                \
  {                                                                                                \
    vkCmdPushConstants(cb,                                                                         \
                       instance->pl.named._p_id,                                                   \
                       VK_SHADER_STAGE_COMPUTE_BIT,                                                \
                       0,                                                                          \
                       instance->config.p.push_sizes.named._p_id,                                  \
                       push);                                                                      \
  }

SPN_VK_TARGET_P_EXPAND()

//
// Bind pipeline set to command buffer
//

#undef SPN_VK_TARGET_P_EXPAND_X
#define SPN_VK_TARGET_P_EXPAND_X(_p_idx, _p_id, _p_descs)                                          \
  SPN_VK_TARGET_P_BIND_PROTO(_p_id)                                                                \
  {                                                                                                \
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, instance->p.named._p_id);                \
  }

SPN_VK_TARGET_P_EXPAND()

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
spn_vk_ds_extents_alloc_block_pool(struct spn_vk * const                                instance,
                                   struct SPN_VK_TARGET_DUTD_NAME(block_pool) * * const block_pool)
{
  // allocate buffers
#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                      \
  spn_vk_extent_alloc(instance,                                                                    \
                      &(*block_pool)->_d_id.entry,                                                 \
                      instance->config.ds.block_pool.mem.sizes._d_id,                              \
                      instance->config.ds.block_pool.mem.props._d_id);

  SPN_VK_TARGET_DS_BLOCK_POOL();
}

void
spn_vk_ds_extents_free_block_pool(struct spn_vk * const                              instance,
                                  struct SPN_VK_TARGET_DUTD_NAME(block_pool) * const block_pool)
{
  // allocate buffers
#undef SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id)                      \
  spn_vk_extent_free(instance, &block_pool->_d_id.entry);

  SPN_VK_TARGET_DS_BLOCK_POOL();
}
#endif

//
//
//
