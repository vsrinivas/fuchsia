// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_H_

//
// Each Vulkan shader's descriptor set layout and associated push
// constants are defined in vk_layouts.h
//
// Defining the layouts once ensures *consistency* between the host C
// source, the GLSL shaders, and the Vulkan pipelines and resources.
//
// This file uses the layout file to provide type-safe access to
// all Vulkan resources.
//
// An spn_vk instance does the following:
//
//   - Takes a Spinel/VK target and creates device-specific instances
//     of all Spinel pipelines.
//
//   - Allocates fixed size pools of pipeline descriptor sets.
//
//   - Enables performant update of descriptor sets using Vulkan
//     update templates.
//
//   - Pumps the scheduler when descriptor sets are unavailable.
//
//   - Defines typed C bindings for updating descriptor sets.
//
//   - Defines typed C bindings for initializing push constants.
//
//   - Defines explicity named pipeline binding functions.
//

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "vk_layouts.h"

//
// No need to know how these are implemented
//

struct spn_device;
struct spn_vk_environment;
struct spn_vk_target;

//
// If you want to see what's happening here with all of the descriptor
// layout expansions, run vk.c through the preprocessor:
//
// clang -I $VULKAN_SDK/include  -I ../.. -I ../../.. -I ../../include/spinel -E  vk.c | clang-format > vk_clang.c
// cl    -I %VULKAN_SDK%\include -I ..\.. -I ..\..\.. -I ..\..\include\spinel -EP vk.c | clang-format > vk_msvc.c
//

//
// Update the descriptor sets
//
// There are currently 10 descriptor sets:
//
//   - block_pool
//   - path copy
//   - fill_cmds
//   - prim_scan
//   - rast_cmds
//   - ttrks
//   - ttcks
//   - place_cmds
//   - styling
//   - surface
//
// Most descriptor sets are ephemeral and sized according to the
// target config.
//
// The following descriptor sets are durable and are either explicitly
// sized or sized using configuration defaults:
//
//   - block_pool
//   - fill_cmds
//   - place_cmds
//   - ttcks
//   - styling
//
// The surface descriptor set is currently the only descriptor that is
// externally defined/allocated/managed:
//
//   - surface
//

//
// Given a target, create an instance of spn_vk
//

struct spn_vk *
spn_vk_create(struct spn_vk_environment * const  environment,
              struct spn_vk_target const * const target);

//
// Resources will be disposed of with the same device and allocator
// that was used for creation.
//

void
spn_vk_dispose(struct spn_vk * const instance, struct spn_vk_environment * const environment);

//
// Get the target configuration structure
//

struct spn_vk_target_config const *
spn_vk_get_config(struct spn_vk const * const instance);

//
// Get the VkPipelineLayout that HotSort will operate on
//

VkPipelineLayout
spn_vk_pl_hotsort(struct spn_vk const * const instance);

//
// Declare host-side descriptor set buffer/image binding structures:
//
//   struct spn_vk_buf_block_pool_bp_ids
//   struct spn_vk_buf_block_pool_bp_blocks
//   ...
//   struct spn_vk_buf_render_surface
//

#define SPN_VK_BUFFER_NAME(ds_id_, name_) struct spn_vk_buf_##ds_id_##_##name_

#define SPN_VK_GLSL_LAYOUT_BUFFER(ds_id_, s_idx_, b_idx_, name_) SPN_VK_BUFFER_NAME(ds_id_, name_)

#define SPN_VK_GLSL_LAYOUT_IMAGE2D(ds_id_, s_idx_, b_idx_, img_type_, name_)

SPN_VK_GLSL_DS_EXPAND()

//
// If the host-side buffer structure is simply:
//
//   struct SPN_VK_BUFFER_NAME(foo,bar) {
//     <type> bar[0];
//   };
//
// it will have a sizeof() equal to type (right?).
//
//

#define SPN_VK_BUFFER_OFFSETOF(ds_id_, name_, member_)                                             \
  OFFSETOF_MACRO(SPN_VK_BUFFER_NAME(ds_id_, name_), member_)

#define SPN_VK_BUFFER_MEMBER_SIZE(ds_id_, name_, member_)                                          \
  MEMBER_SIZE_MACRO(SPN_VK_BUFFER_NAME(ds_id_, name_), member_)

//
// Define host-side pipeline push constant structures
//
//   struct spn_vk_push_block_pool_init
//   ...
//   struct spn_vk_push_render
//
//

#define SPN_VK_PUSH_NAME(p_id_) struct spn_vk_push_##p_id_

#undef SPN_VK_HOST_DS
#define SPN_VK_HOST_DS(p_id_, ds_idx_, ds_id_)

#undef SPN_VK_HOST_PUSH
#define SPN_VK_HOST_PUSH(p_id_, p_pc_) SPN_VK_PUSH_NAME(p_id_){ p_pc_ };

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_id_x, p_id_, _p_descs) _p_descs

SPN_VK_P_EXPAND()

//
// Declare descriptor acquire/release/update functions
//

#define SPN_VK_DS_TYPE(ds_id_) struct spn_vk_ds_##ds_id_##_t
#define SPN_VK_DS_TYPE_DECLARE(ds_id_)                                                             \
  SPN_VK_DS_TYPE(ds_id_)                                                                           \
  {                                                                                                \
    uint32_t idx;                                                                                  \
  }

#define SPN_VK_DS_ACQUIRE_FUNC(ds_id_) spn_vk_ds_acquire_##ds_id_
#define SPN_VK_DS_UPDATE_FUNC(ds_id_) spn_vk_ds_update_##ds_id_
#define SPN_VK_DS_RELEASE_FUNC(ds_id_) spn_vk_ds_release_##ds_id_

#define SPN_VK_DS_ACQUIRE_PROTO(ds_id_)                                                            \
  void SPN_VK_DS_ACQUIRE_FUNC(ds_id_)(struct spn_vk * const          instance,                     \
                                      struct spn_device * const      device,                       \
                                      SPN_VK_DS_TYPE(ds_id_) * const ds)

#define SPN_VK_DS_UPDATE_PROTO(ds_id_)                                                             \
  void SPN_VK_DS_UPDATE_FUNC(ds_id_)(struct spn_vk * const             instance,                   \
                                     struct spn_vk_environment * const environment,                \
                                     SPN_VK_DS_TYPE(ds_id_) const ds)

#define SPN_VK_DS_RELEASE_PROTO(ds_id_)                                                            \
  void SPN_VK_DS_RELEASE_FUNC(ds_id_)(struct spn_vk * const instance,                              \
                                      SPN_VK_DS_TYPE(ds_id_) const ds)

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_)                                                   \
  SPN_VK_DS_TYPE_DECLARE(ds_id_);                                                                  \
  SPN_VK_DS_ACQUIRE_PROTO(ds_id_);                                                                 \
  SPN_VK_DS_UPDATE_PROTO(ds_id_);                                                                  \
  SPN_VK_DS_RELEASE_PROTO(ds_id_);

SPN_VK_DS_EXPAND()

//
// Get references to descriptor set entries
//

#define SPN_VK_DS_GET_FUNC(ds_id_, d_id_) spn_vk_ds_get_##ds_id_##_##d_id_

#define SPN_VK_DS_GET_PROTO_STORAGE_BUFFER(ds_id_, d_id_)                                          \
  VkDescriptorBufferInfo * SPN_VK_DS_GET_FUNC(ds_id_, d_id_)(struct spn_vk * const instance,       \
                                                             SPN_VK_DS_TYPE(ds_id_) const ds)

#define SPN_VK_DS_GET_PROTO_STORAGE_IMAGE(ds_id_, d_id_)                                           \
  VkDescriptorImageInfo * SPN_VK_DS_GET_FUNC(ds_id_, d_id_)(struct spn_vk * const instance,        \
                                                            SPN_VK_DS_TYPE(ds_id_) const ds)

#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(ds_id_, d_id_x_, d_ext_, d_id_)                            \
  SPN_VK_DS_GET_PROTO_STORAGE_BUFFER(ds_id_, d_id_);

#undef SPN_VK_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_DESC_TYPE_STORAGE_IMAGE(ds_id_, d_id_x_, d_ext_, d_id_)                             \
  SPN_VK_DS_GET_PROTO_STORAGE_IMAGE(ds_id_, d_id_);

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(ds_idx_, ds_id_, ds_) ds_

SPN_VK_DS_EXPAND()

//
// Bind a pipeline-specific descriptor set to a command buffer
//

#define SPN_VK_DS_BIND_FUNC(p_id_, ds_id_) spn_vk_ds_bind_##p_id_##_##ds_id_

#define SPN_VK_DS_BIND_PROTO(p_id_, ds_id_)                                                        \
  void SPN_VK_DS_BIND_FUNC(p_id_, ds_id_)(struct spn_vk * const instance,                          \
                                          VkCommandBuffer       cb,                                \
                                          SPN_VK_DS_TYPE(ds_id_) const ds)

#undef SPN_VK_HOST_DS
#define SPN_VK_HOST_DS(p_id_, ds_idx_, ds_id_) SPN_VK_DS_BIND_PROTO(p_id_, ds_id_);

#undef SPN_VK_HOST_PUSH
#define SPN_VK_HOST_PUSH(p_id_, p_pc_)

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_id_x, p_id_, p_descs_) p_descs_

SPN_VK_P_EXPAND()

//
// Write push constants to command buffer
//

#define SPN_VK_P_PUSH_FUNC(p_id_) spn_vk_p_push_##p_id_

#define SPN_VK_P_PUSH_PROTO(p_id_)                                                                 \
  SPN_VK_PUSH_NAME(p_id_);                                                                         \
  void SPN_VK_P_PUSH_FUNC(p_id_)(struct spn_vk * const                 instance,                   \
                                 VkCommandBuffer                       cb,                         \
                                 SPN_VK_PUSH_NAME(p_id_) const * const push)

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_id_x, p_id_, p_descs_) SPN_VK_P_PUSH_PROTO(p_id_);

SPN_VK_P_EXPAND()

//
// Bind pipeline to command buffer
//

#define SPN_VK_P_BIND_FUNC(p_id_) spn_vk_p_bind_##p_id_

#define SPN_VK_P_BIND_PROTO(p_id_)                                                                 \
  void SPN_VK_P_BIND_FUNC(p_id_)(struct spn_vk * const instance, VkCommandBuffer cb)

#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(p_id_x, p_id_, p_descs_) SPN_VK_P_BIND_PROTO(p_id_);

SPN_VK_P_EXPAND()

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_H_
