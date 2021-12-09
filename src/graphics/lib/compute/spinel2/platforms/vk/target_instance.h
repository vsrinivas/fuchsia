// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_INSTANCE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_INSTANCE_H_

//
//
//

#include "radix_sort/platforms/vk/radix_sort_vk.h"
#include "shaders/pipelines.h"
#include "spinel/platforms/vk/spinel_vk_types.h"
#include "target.h"

//
//
//
struct spinel_pipeline_layouts_named
{
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_) VkPipelineLayout pipeline_;

  SPN_P_EXPAND()
};

struct spinel_pipelines_named
{
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(pipeline_, push_) VkPipeline pipeline_;

  SPN_P_EXPAND()
};

//
//
//
struct spinel_target_instance
{
  struct spinel_target_config config;

  union
  {
    struct spinel_pipeline_layouts_named named;
    VkPipelineLayout                     handles[SPN_P_COUNT];
  } pipeline_layouts;

  union
  {
    struct spinel_pipelines_named named;
    VkPipeline                    handles[SPN_P_COUNT];
  } pipelines;

  struct radix_sort_vk * rs;
};

//
//
//
bool
spinel_target_instance_create(struct spinel_target_instance * ti,
                              VkDevice                        d,
                              VkAllocationCallbacks const *   ac,
                              VkPipelineCache                 pc,
                              struct spinel_vk_target const * target);

//
//
//
void
spinel_target_instance_destroy(struct spinel_target_instance *     ti,
                               VkDevice                            d,
                               VkAllocationCallbacks const * const ac);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_INSTANCE_H_
