// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#include "shaders/pipelines.h"
#include "target_requirements.h"

//
//
//

#define SPN_HEADER_MAGIC 0x4C4E5053  // "SPNL"

//
//
//

struct spinel_target_group_size
{
  // clang-format off
  uint32_t workgroup     : 24;
  uint32_t subgroup_log2 : 8;
  // clang-format on
};

//
//
//

struct spinel_target_allocator
{
  VkMemoryPropertyFlags properties;
  VkBufferUsageFlags    usage;
};

//
// This structure packages target-specific configuration parameters.
//

struct spinel_target_config
{
  //
  // Allocators
  //
  struct
  {
    struct  // DEVICE ALLOCATORS
    {
      struct spinel_target_allocator drw;         // device read-write
      struct spinel_target_allocator hw_dr;       // host write / device read
      struct spinel_target_allocator hrw_dr;      // host read-write / device read
      struct spinel_target_allocator drw_shared;  // device read-write on 1 or 2 queue families
    } device;
  } allocator;

  //
  // Deps concurrency
  //
  struct
  {
    struct
    {
      struct
      {
        struct
        {
          uint32_t size;   // Size of immediate semaphore pool is
          uint32_t count;  // (pool.size * pool.count)
        } pool;            //
      } immediate;         //
                           //
      struct               //
      {                    //
        uint32_t size;     // Size of delayed semaphore pool
      } delayed;           //
    } semaphores;          //
  } deps;

  //
  // Tile size
  //
  struct
  {
    uint32_t width_log2;
    uint32_t height_log2;
  } tile;

  //
  // Subpixel resolution
  //
  struct
  {
    uint32_t width_log2;
    uint32_t height_log2;
  } pixel;

  //
  // Block pool size
  //
  struct
  {
    uint32_t block_dwords_log2;
    uint32_t subblock_dwords_log2;
    uint32_t ids_per_invocation;
  } block_pool;

  //
  // Path builder
  //
  struct
  {
    struct
    {
      uint32_t dispatches;  // number of in-flight dispatches
      uint32_t ring;        // number of blocks & cmds in ring
      uint32_t eager;       // number of blocks that will force an eager launch
    } size;
  } path_builder;

  //
  // Raster builder
  //
  struct
  {
    struct
    {
      uint32_t dispatches;  // number of in-flight dispatches
      uint32_t ring;        // number of fill commands in ring shared across all dispatches
      uint32_t eager;       // number of fill commands that will force an eager launch of a dispatch
      uint32_t cohort;      // max number of rasters in a cohort
      uint32_t cmds;        // max rast cmds per dispatch emitted by FILLS_EXPAND without error
      uint32_t ttrks;       // max ttrks per dispatch emitted by RASTERIZE_XXX without error
    } size;

    struct
    {
      uint32_t rows;
    } fill_scan;
  } raster_builder;

  //
  // Composition
  //
  struct
  {
    struct
    {
      uint32_t dispatches;  // number of in-flight dispatches
      uint32_t ring;        // number of commands in ring
      uint32_t eager;       // number of commands that will force an eager launch
      uint32_t ttcks;       // max number of ttcks that can be emitted by successive PLACE shaders
      uint32_t rasters;     // max number of retained rasters
    } size;
  } composition;

  //
  // Swapchain
  //
  struct
  {
    VkSharingMode sharing_mode;  // Exclusive or concurrent
    uint32_t      texel_size;    // How many bytes per texel?
  } swapchain;

  //
  // Reclamation
  //
  struct
  {
    struct
    {
      uint32_t dispatches;  // number of in-flight dispatches
      uint32_t paths;       // number of paths in reclamation ring
      uint32_t rasters;     // number of rasters in reclamation ring
      uint32_t eager;       // number of handles that will force an eager launch
    } size;
  } reclaim;

  //
  // Pipeline workgroup and subgroup sizes
  //
  union
  {
    struct
    {
#undef SPN_P_EXPAND_X
#define SPN_P_EXPAND_X(name_, push_) struct spinel_target_group_size name_;

      SPN_P_EXPAND()
    } named;

    struct spinel_target_group_size array[SPN_P_COUNT];

  } group_sizes;
};

//
//
//

struct spinel_target_header
{
  uint32_t                       magic;       // magic header dword
  union spinel_target_extensions extensions;  // target device extensions
  union spinel_target_features   features;    // target device features
  struct spinel_target_config    config;      // target configuration
  uint32_t                       modules[];   // SPIR-V modules
};

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_H_
