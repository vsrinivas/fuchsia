// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "expand_x.h"

//
// GLSL EXTENSIONS
//

#ifdef VULKAN

// #extension GL_AMD_gpu_shader_half_float                    : require
// #extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
// #extension GL_EXT_shader_explicit_arithmetic_types_int8    : require

// #define SPN_ENABLE_EXTENSION_SUBGROUP_UNIFORM // useful on AMD GCN

#endif

//
// DEVICE-SPECIFIC
//

#define SPN_DEVICE_INTEL_GEN8                                     1
#define SPN_DEVICE_SUBGROUP_SIZE_LOG2                             3   // 8
#define SPN_DEVICE_MAX_PUSH_CONSTANTS_SIZE                        128 // bytes

//
// TILE CONFIGURATION
//

#define SPN_TILE_WIDTH_LOG2                                       3 // 8
#define SPN_TILE_HEIGHT_LOG2                                      3 // 8

//
// BLOCK POOL CONFIGURATION
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_BLOCK_POOL_BLOCK_DWORDS_LOG2                          7
#define SPN_BLOCK_POOL_SUBBLOCK_DWORDS_LOG2                       SPN_TILE_WIDTH_LOG2
#define SPN_KERNEL_BLOCK_POOL_INIT_BP_IDS_PER_WORKGROUP           (SPN_KERNEL_BLOCK_POOL_INIT_WORKGROUP_SIZE *          \
                                                                   SPN_KERNEL_BLOCK_POOL_INIT_BP_IDS_PER_INVOCATION)

//
// KERNEL: BLOCK POOL INIT
//

#define SPN_KERNEL_BLOCK_POOL_INIT_WORKGROUP_SIZE                 128
#define SPN_KERNEL_BLOCK_POOL_INIT_BP_IDS_PER_INVOCATION          16

//
// KERNEL: PATHS ALLOC
//
// Note that this workgroup only uses one lane but, depending on the
// target, it might be necessary to launch at least a subgroup.
//

#define SPN_KERNEL_PATHS_ALLOC_WORKGROUP_SIZE                     1

//
// KERNEL: PATHS COPY
//

#define SPN_KERNEL_PATHS_COPY_SUBGROUP_SIZE                       SPN_DEVICE_SUBGROUP_SIZE
#define SPN_KERNEL_PATHS_COPY_WORKGROUP_SIZE                      SPN_KERNEL_PATHS_COPY_SUBGROUP_SIZE

//
// KERNEL: FILLS SCAN
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_KERNEL_FILLS_SCAN_SUBGROUP_SIZE                       SPN_DEVICE_SUBGROUP_SIZE
#define SPN_KERNEL_FILLS_SCAN_WORKGROUP_SIZE                      SPN_KERNEL_FILLS_SCAN_SUBGROUP_SIZE

#define SPN_KERNEL_FILLS_SCAN_EXPAND()                            SPN_EXPAND_4()
#define SPN_KERNEL_FILLS_SCAN_EXPAND_I_LAST                       3

//
// KERNEL: FILLS EXPAND
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_KERNEL_FILLS_EXPAND_SUBGROUP_SIZE                     SPN_DEVICE_SUBGROUP_SIZE
#define SPN_KERNEL_FILLS_EXPAND_WORKGROUP_SIZE                    SPN_KERNEL_FILLS_EXPAND_SUBGROUP_SIZE

//
// KERNEL: FILLS DISPATCH
//

#define SPN_KERNEL_FILLS_DISPATCH_SUBGROUP_SIZE                   SPN_DEVICE_SUBGROUP_SIZE

//
// KERNEL: RASTERIZE
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_KERNEL_RASTERIZE_SUBGROUP_SIZE                        SPN_DEVICE_SUBGROUP_SIZE
#define SPN_KERNEL_RASTERIZE_WORKGROUP_SIZE                       SPN_KERNEL_RASTERIZE_SUBGROUP_SIZE

//
// KERNEL: SEGMENT TTRK
//

#define SPN_KERNEL_SEGMENT_TTRK_METAS_SIZE                        SPN_RASTER_COHORT_METAS_SIZE

//
// KERNEL: RASTERS ALLOC
//

#define SPN_KERNEL_RASTERS_ALLOC_SUBGROUP_SIZE                    SPN_DEVICE_SUBGROUP_SIZE
#define SPN_KERNEL_RASTERS_ALLOC_WORKGROUP_SIZE                   SPN_KERNEL_RASTERS_ALLOC_SUBGROUP_SIZE

// can reduce this to force earlier launches of smaller grids
#define SPN_KERNEL_RASTERS_ALLOC_METAS_SIZE                       SPN_KERNEL_SEGMENT_TTRK_METAS_SIZE

//
// KERNEL: RASTERS PREFIX
//

#define SPN_KERNEL_RASTERS_PREFIX_SUBGROUP_SIZE_LOG2              SPN_DEVICE_SUBGROUP_SIZE_LOG2
#define SPN_KERNEL_RASTERS_PREFIX_WORKGROUP_SIZE                  SPN_KERNEL_RASTERS_PREFIX_SUBGROUP_SIZE

#define SPN_KERNEL_RASTERS_PREFIX_KEYS_LOAD_LOG2                  SPN_KERNEL_RASTERS_PREFIX_SUBGROUP_SIZE_LOG2
#define SPN_KERNEL_RASTERS_PREFIX_EXPAND_SIZE                     SPN_KERNEL_RASTERS_PREFIX_KEYS_LOAD

//
// KERNEL: PLACE
//

#define SPN_KERNEL_PLACE_SUBGROUP_SIZE_LOG2                       SPN_DEVICE_SUBGROUP_SIZE_LOG2
#define SPN_KERNEL_PLACE_SUBGROUP_SIZE                            (1<<SPN_KERNEL_PLACE_SUBGROUP_SIZE_LOG2)
#define SPN_KERNEL_PLACE_WORKGROUP_SIZE                           SPN_KERNEL_PLACE_SUBGROUP_SIZE

#define SPN_KERNEL_PLACE_BLOCK_EXPAND_SIZE                        ((SPN_BLOCK_POOL_BLOCK_DWORDS / 2) / SPN_KERNEL_PLACE_SUBGROUP_SIZE)

//
// KERNEL: SEGMENT TTCK
//

//
// KERNEL: RENDER
//

#define SPN_KERNEL_RENDER_LGF_USE_SHUFFLE
#define SPN_KERNEL_RENDER_TTCKS_USE_SHUFFLE
#define SPN_KERNEL_RENDER_STYLING_CMDS_USE_SHUFFLE

#define SPN_KERNEL_RENDER_TILE_CHANNEL_IS_FLOAT
// #define SPN_KERNEL_RENDER_TILE_CHANNEL_IS_FP16    // test once compiler supports VK_KHR_shader_float16_int8
// #define SPN_KERNEL_RENDER_TILE_CHANNEL_IS_FP16X2  // test once compiler supports VK_KHR_shader_float16_int8

#define SPN_KERNEL_RENDER_SUBGROUP_SIZE_LOG2                      SPN_DEVICE_SUBGROUP_SIZE_LOG2
#define SPN_KERNEL_RENDER_WORKGROUP_SIZE_LOG2                     SPN_DEVICE_SUBGROUP_SIZE_LOG2

#define SPN_KERNEL_RENDER_STORAGE_STYLING                         readonly buffer // could also be a uniform

#ifdef SPN_KERNEL_RENDER_SURFACE_IS_IMAGE
#define SPN_KERNEL_RENDER_SURFACE_TYPE                            rgba8
#define SPN_KERNEL_RENDER_COLOR_ACC_PACK(rgba)                    rgba
#else
#define SPN_KERNEL_RENDER_SURFACE_TYPE                            uint
#define SPN_KERNEL_RENDER_COLOR_ACC_PACK(rgba)                    packUnorm4x8(rgba)
#endif

//
// KERNEL: PATHS RECLAIM
//

#define SPN_KERNEL_PATHS_RECLAIM_SUBGROUP_SIZE                    SPN_DEVICE_SUBGROUP_SIZE
#define SPN_KERNEL_PATHS_RECLAIM_WORKGROUP_SIZE                   SPN_KERNEL_PATHS_RECLAIM_SUBGROUP_SIZE
#define SPN_KERNEL_PATHS_RECLAIM_MAX_RECLAIM_IDS                  (SPN_DEVICE_MAX_PUSH_CONSTANTS_SIZE / 4 - 1)

#define SPN_KERNEL_PATHS_RECLAIM_EXPAND_SIZE                      (SPN_BLOCK_POOL_BLOCK_DWORDS / SPN_KERNEL_PATHS_RECLAIM_SUBGROUP_SIZE)

//
// KERNEL: RASTERS RECLAIM
//

#define SPN_KERNEL_RASTERS_RECLAIM_SUBGROUP_SIZE                  SPN_DEVICE_SUBGROUP_SIZE
#define SPN_KERNEL_RASTERS_RECLAIM_WORKGROUP_SIZE                 SPN_KERNEL_RASTERS_RECLAIM_SUBGROUP_SIZE
#define SPN_KERNEL_RASTERS_RECLAIM_MAX_RECLAIM_IDS                (SPN_DEVICE_MAX_PUSH_CONSTANTS_SIZE / 4 - 1)

#define SPN_KERNEL_RASTERS_RECLAIM_EXPAND_SIZE                    (SPN_BLOCK_POOL_BLOCK_DWORDS / SPN_KERNEL_RASTERS_RECLAIM_SUBGROUP_SIZE / 2)

//
//
//
