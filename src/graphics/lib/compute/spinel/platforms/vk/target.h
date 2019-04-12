// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdbool.h>

#include "target_config.h"

//
//
//

struct spn_device_vk;
struct spn_device;

//
// If you want to see what's happening here with all of the descriptor
// layout expansions, run target.c through the preprocessor:
//
// clang -I %VULKAN_SDK%\include -I ..\.. -I ..\..\.. -E  target.c | clang-format > target_clang.c
// cl    -I %VULKAN_SDK%\include -I ..\.. -I ..\..\.. -EP target.c | clang-format > target_msvc.c
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
// Create a Spinel target
//

struct spn_target *
spn_target_create(struct spn_device_vk          * const vk,
                  struct spn_target_image const * const target_image);

//
// Resources will be disposed of with the same device and allocator
// that was used for creation.
//

void
spn_target_dispose(struct spn_target    * const target,
                   struct spn_device_vk * const vk);

//
// Get the target configuration structure
//

struct spn_target_config const *
spn_target_get_config(struct spn_target const * const target);

//
// Define host-side descriptor set buffer/image binding structures:
//
//   struct spn_target_buf_block_pool_bp_ids
//   struct spn_target_buf_block_pool_bp_blocks
//   ...
//   struct spn_target_buf_render_surface
//

#define SPN_TARGET_BUFFER_NAME(_ds_id,_name)    \
  struct spn_target_buf_##_ds_id##_##_name

#define SPN_TARGET_GLSL_LAYOUT_BUFFER(_ds_id,_s_idx,_b_idx,_name)     \
  SPN_TARGET_BUFFER_NAME(_ds_id,_name)

#define SPN_TARGET_GLSL_LAYOUT_IMAGE2D(_ds_id,_s_idx,_b_idx,_img_type,_name)

#define SPN_TARGET_GLSL_BUFFER_INSTANCE(_name)

SPN_TARGET_GLSL_DS_EXPAND();

//
// If the host-side buffer structure is simply:
//
//   struct SPN_TARGET_BUFFER_NAME(foo,bar) {
//     <type> bar[0];
//   };
//
// it will have a sizeof() equal to type (right?).
//
//

#define SPN_TARGET_BUFFER_OFFSETOF(_ds_id,_name,_member)        \
  OFFSET_OF_MACRO(SPN_TARGET_BUFFER_NAME(_ds_id,_name),_member)

//
// Define host-side pipeline push constant structures
//
//   struct spn_target_push_block_pool_init
//   ...
//   struct spn_target_push_render
//
//

#define SPN_TARGET_PUSH_NAME(_p_id) struct spn_target_push_##_p_id

#undef  SPN_TARGET_VK_DS
#define SPN_TARGET_VK_DS(_p_id,_ds_idx,_ds_id)

#undef  SPN_TARGET_VK_PUSH
#define SPN_TARGET_VK_PUSH(_p_id,_p_pc)         \
  SPN_TARGET_PUSH_NAME(_p_id) {                 \
    _p_pc                                       \
  };

#undef  SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx,_p_id,_p_descs)  \
  _p_descs

SPN_TARGET_P_EXPAND()

//
// Declare descriptor acquire/release/update functions
//

#define SPN_TARGET_DS_TYPE(_ds_id)           struct spn_target_ds_##_ds_id##_t
#define SPN_TARGET_DS_TYPE_DECLARE(_ds_id)   SPN_TARGET_DS_TYPE(_ds_id) { uint32_t idx; }

#define SPN_TARGET_DS_ACQUIRE_FUNC(_ds_id)   spn_target_ds_acquire_##_ds_id
#define SPN_TARGET_DS_UPDATE_FUNC(_ds_id)    spn_target_ds_update_##_ds_id
#define SPN_TARGET_DS_RELEASE_FUNC(_ds_id)   spn_target_ds_release_##_ds_id

#define SPN_TARGET_DS_ACQUIRE_PROTO(_ds_id)                                     \
  void                                                                          \
  SPN_TARGET_DS_ACQUIRE_FUNC(_ds_id)(struct spn_target          * const target, \
                                     struct spn_device          * const device, \
                                     SPN_TARGET_DS_TYPE(_ds_id) * const ds)

#define SPN_TARGET_DS_UPDATE_PROTO(_ds_id)                                      \
  void                                                                          \
  SPN_TARGET_DS_UPDATE_FUNC(_ds_id)(struct spn_target          * const target,  \
                                    struct spn_device_vk       * const vk,      \
                                    SPN_TARGET_DS_TYPE(_ds_id)   const ds)

#define SPN_TARGET_DS_RELEASE_PROTO(_ds_id)                                     \
  void                                                                          \
  SPN_TARGET_DS_RELEASE_FUNC(_ds_id)(struct spn_target        * const target,   \
                                     SPN_TARGET_DS_TYPE(_ds_id) const ds)

#undef  SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx,_ds_id,_ds)      \
  SPN_TARGET_DS_TYPE_DECLARE(_ds_id);                   \
  SPN_TARGET_DS_ACQUIRE_PROTO(_ds_id);                  \
  SPN_TARGET_DS_UPDATE_PROTO(_ds_id);                   \
  SPN_TARGET_DS_RELEASE_PROTO(_ds_id);

SPN_TARGET_DS_EXPAND()

//
// Get references to descriptor set entries
//

#define SPN_TARGET_DS_GET_FUNC(_ds_id,_d_id) spn_target_ds_get_##_ds_id##_##_d_id

#define SPN_TARGET_DS_GET_PROTO_STORAGE_BUFFER(_ds_id,_d_id)                          \
  VkDescriptorBufferInfo *                                                            \
  SPN_TARGET_DS_GET_FUNC(_ds_id,_d_id)(struct spn_target        * const target,       \
                                       SPN_TARGET_DS_TYPE(_ds_id) const ds)

#define SPN_TARGET_DS_GET_PROTO_STORAGE_IMAGE(_ds_id,_d_id)                           \
  VkDescriptorImageInfo *                                                             \
  SPN_TARGET_DS_GET_FUNC(_ds_id,_d_id)(struct spn_target        * const target,       \
                                       SPN_TARGET_DS_TYPE(_ds_id) const ds);

#undef  SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id,_d_idx,_d_ext,_d_id) \
  SPN_TARGET_DS_GET_PROTO_STORAGE_BUFFER(_ds_id,_d_id);

#undef  SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id,_d_idx,_d_ext,_d_id)  \
  SPN_TARGET_DS_GET_PROTO_STORAGE_IMAGE(_ds_id,_d_id);

#undef  SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx,_ds_id,_ds)      \
  _ds

SPN_TARGET_DS_EXPAND()

//
// Bind a pipeline-specific descriptor set to a command buffer
//

#define SPN_TARGET_DS_BIND_FUNC(_p_id,_ds_id)  spn_target_ds_bind_##_p_id##_##_ds_id

#define SPN_TARGET_DS_BIND_PROTO(_p_id,_ds_id)                                        \
  void                                                                                \
  SPN_TARGET_DS_BIND_FUNC(_p_id,_ds_id)(struct spn_target        * const target,      \
                                        VkCommandBuffer                  cb,          \
                                        SPN_TARGET_DS_TYPE(_ds_id) const ds)

#undef  SPN_TARGET_VK_DS
#define SPN_TARGET_VK_DS(_p_id,_ds_idx,_ds_id)  \
  SPN_TARGET_DS_BIND_PROTO(_p_id,_ds_id);

#undef  SPN_TARGET_VK_PUSH
#define SPN_TARGET_VK_PUSH(_p_id,_p_pc)

#undef  SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx,_p_id,_p_descs)   \
  _p_descs

SPN_TARGET_P_EXPAND()

//
// Write push constants to command buffer
//

#define SPN_TARGET_P_PUSH_FUNC(_p_id)  spn_target_p_push_##_p_id

#define SPN_TARGET_P_PUSH_PROTO(_p_id)                                              \
  SPN_TARGET_PUSH_NAME(_p_id);                                                      \
  void                                                                              \
  SPN_TARGET_P_PUSH_FUNC(_p_id)(struct spn_target                 * const target,   \
                                VkCommandBuffer                           cb,       \
                                SPN_TARGET_PUSH_NAME(_p_id) const * const push)

#undef  SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx,_p_id,_p_descs)      \
  SPN_TARGET_P_PUSH_PROTO(_p_id);

SPN_TARGET_P_EXPAND()

//
// Bind pipeline to command buffer
//

#define SPN_TARGET_P_BIND_FUNC(_p_id)  spn_target_p_bind_##_p_id

#define SPN_TARGET_P_BIND_PROTO(_p_id)                                \
  void                                                                \
  SPN_TARGET_P_BIND_FUNC(_p_id)(struct spn_target * const target,     \
                                VkCommandBuffer           cb)

#undef  SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx,_p_id,_p_descs)   \
  SPN_TARGET_P_BIND_PROTO(_p_id);

SPN_TARGET_P_EXPAND()

//
//
//
