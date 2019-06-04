// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPN_VK_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPN_VK_TARGET_H_

//
//
//

#include "common/macros.h"
#include "spn_vk_layouts.h"

//
// This structure packages all of the parameters and SPIR-V kernels
// for a target architecture.
//

struct spn_vk_target_config
{
  //
  // clang-format off
  //

  //
  // host allocators
  //
  struct {
    struct {
      struct {
        uint32_t alignment;
      } perm;
      struct {
        uint32_t subbufs;
        uint32_t size;
        uint32_t alignment;
      } temp;
    } host;
    struct {
      struct {
        uint32_t subbufs;
        uint32_t size;
      } temp;
    } device;
  } allocator;

  //
  // max submitted command buffers
  //
  struct {
    uint32_t     size;
  } fence_pool;

  //
  // target subgroup size
  //
  uint32_t       subgroup_size_log2;

  struct {
    uint32_t     width_log2;
    uint32_t     height_log2;
  } tile;

  //
  // block pool size
  //
  struct {
    uint32_t     block_dwords_log2;
    uint32_t     subblock_dwords_log2;
    uint32_t     ids_per_workgroup;
  } block_pool;

  struct {                    // FIXME -- put ring host_coherent allocation flags here
    uint32_t     ring_size;   // number of blocks & cmds in ring
    uint32_t     eager_size;  // number of blocks that will force an eager launch
  } path_builder;

  struct {
    struct {
      struct {
                              // FIXME -- put ring host_coherent and device_local allocation flags here
        uint32_t h;           // index of host   vk allocator
        uint32_t d;           // index of device vk allocator
      } rings;
    } vk;

    struct {
      uint32_t   ring;        // number of commands in ring
      uint32_t   eager;       // number of commands that will force an eager launch
      uint32_t   cohort;      // max number of rasters in ring
      uint32_t   cmds;        // max number of rast cmds that can be emitted by FILLS_EXPAND
      uint32_t   ttrks;       // max number of ttrks that can be emitted by RASTERIZE_XXX
    } size;
  } raster_builder;

  struct {
    struct {
      uint32_t   h;           // index of host   vk allocator
      uint32_t   d;           // index of device vk allocator
    } vk;
  } styling;

  struct {
    struct {
      struct {                // FIXME -- put ring host_coherent and device_local allocation flags here
        uint32_t h;           // index of host   vk allocator
        uint32_t d;           // index of device vk allocator
      } rings;
    } vk;

    struct {
      uint32_t   ring;        // number of commands in ring
      uint32_t   eager;       // number of commands that will force an eager launch
      uint32_t   cmds;        // max number of place cmds in the composition
      uint32_t   ttcks;       // max number of ttcks that can be emitted by successive PLACE shaders
      uint32_t   rasters;     // max number of retained rasters
    } size;
  } composition;

  //
  // clang-format on
  //

  //
  // descriptors
  //
  struct
  {
#undef SPN_VK_DESC_TYPE_STORAGE_BUFFER
#define SPN_VK_DESC_TYPE_STORAGE_BUFFER(_ds_id, _d_idx, _d_ext, _d_id) uint32_t _d_id;

#undef SPN_VK_DESC_TYPE_STORAGE_IMAGE
#define SPN_VK_DESC_TYPE_STORAGE_IMAGE(_ds_id, _d_idx, _d_ext, _d_id)                              \
  uint32_t _d_id;  // do nothing for now

#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                                   \
  struct                                                                                           \
  {                                                                                                \
    uint32_t sets;                                                                                 \
  } _ds_id;

    SPN_VK_DS_EXPAND()

  } ds;

  //
  // descriptor extents
  //
  struct
  {
#undef SPN_VK_DS_EXPAND_X
#define SPN_VK_DS_EXPAND_X(_ds_idx, _ds_id, _ds)                                                   \
  struct                                                                                           \
  {                                                                                                \
    struct                                                                                         \
    {                                                                                              \
      _ds                                                                                          \
    } props;                                                                                       \
  } _ds_id;

    SPN_VK_DS_EXPAND()

  } ds_extents;

  //
  // pipelines
  //
  // - push constant sizes by name and index
  //
  struct
  {
    union
    {
      struct
      {
#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(_p_idx, _p_id, _p_descs) uint32_t _p_id;

        SPN_VK_P_EXPAND()
      } named;
      uint32_t array[SPN_VK_P_COUNT];
    } push_sizes;
  } p;
};

//
// For now, a known number of kernels are appended end-to-end with a
// leading little-endian length followed by a SPIR-V module.
//
// The entry point for each kernel is "main".
//
// When the tools support packaging multiple named compute shaders in
// one SPIR-V module then reevaluate this encoding.
//

struct spn_vk_target
{
  struct spn_vk_target_config config;
  uint32_t                    modules[];
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPN_VK_TARGET_H_
