// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include "common/macros.h"
#include "target_layouts.h"

//
// This structure packages all of the parameters and SPIR-V kernels
// for a target architecture.
//

struct spn_target_config
{
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

  struct {
    // FIXME -- put ring host_coherent allocation flags here
    uint32_t     ring_size;   // number of blocks & cmds in ring
    uint32_t     eager_size;  // number of blocks that will force an eager launch
  } path_builder;

  struct {
    struct {
      struct {
        uint32_t h;           // index of host   vk allocator
        uint32_t d;           // index of device vk allocator
      } rings;

      // copyback

    } vk;

    // FIXME -- put ring host_coherent and device_local allocation flags here
    struct {
      uint32_t   ring;        // number of commands in ring
      uint32_t   eager;       // number of commands that will force an eager launch
      uint32_t   cohort;      // max number of rasters in ring
      uint32_t   rast_cmds;   // max number of rast_cmds that can be emitted by FILLS_EXPAND
      uint32_t   ttrks;       // max number of ttrks that can be emitted by RASTERIZE_XXX
    } size;
  } raster_builder;

  struct {
    struct {
      uint32_t h;             // index of host   vk allocator
      uint32_t d;             // index of device vk allocator
    } vk;
  } styling;

  //
  // descriptors
  //
  struct {

#undef  SPN_TARGET_DESC_TYPE_STORAGE_BUFFER
#define SPN_TARGET_DESC_TYPE_STORAGE_BUFFER(_ds_id,_d_idx,_d_ext,_d_id)  uint32_t _d_id;

#undef  SPN_TARGET_DESC_TYPE_STORAGE_IMAGE
#define SPN_TARGET_DESC_TYPE_STORAGE_IMAGE(_ds_id,_d_idx,_d_ext,_d_id)   uint32_t _d_id; // do nothing for now

#undef  SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx,_ds_id,_ds)      \
    struct {                                            \
      uint32_t sets;                                    \
    } _ds_id;

    SPN_TARGET_DS_EXPAND()

  } ds;

  //
  // descriptor extents
  //
  struct {

#undef  SPN_TARGET_DS_EXPAND_X
#define SPN_TARGET_DS_EXPAND_X(_ds_idx,_ds_id,_ds)      \
    struct {                                            \
      struct {                                          \
        _ds                                             \
      } props;                                          \
    } _ds_id;

    SPN_TARGET_DS_EXPAND()

  } ds_extents;

  //
  // pipelines
  //
  // - push constant sizes by name and index
  //
  struct {
    union {
      struct {
#undef  SPN_TARGET_P_EXPAND_X
#define SPN_TARGET_P_EXPAND_X(_p_idx,_p_id,_p_descs)      \
        uint32_t _p_id;

        SPN_TARGET_P_EXPAND()
      } named;
      uint32_t array[SPN_TARGET_P_COUNT];
    } push_sizes;
  } p;
};

//
// For now, kernels are appended end-to-end with a leading big-endian
// length followed by a SPIR-V binary.
//
// The entry point for each kernel is "main".
//
// When the tools support packaging multiple named compute shaders in
// one SPIR-V module then reevaluate this encoding.
//

struct spn_target_image
{
  struct spn_target_config config;
  ALIGN_MACRO(4) uint8_t   modules[]; // modules[] must start on 32-bit boundary
};

//
//
//
