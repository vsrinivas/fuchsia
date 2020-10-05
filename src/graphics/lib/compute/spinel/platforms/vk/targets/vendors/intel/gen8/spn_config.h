// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TARGETS_VENDORS_INTEL_GEN8_SPN_CONFIG_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TARGETS_VENDORS_INTEL_GEN8_SPN_CONFIG_H_

//
// clang-format off
//

#include "expand_x.h"

//
// GLSL EXTENSIONS
//

#ifdef VULKAN

#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require

#define SPN_EXT_ENABLE_SUBGROUP_UNIFORM                           1

#endif

//
// DEVICE-SPECIFIC
//

#define SPN_DEVICE_INTEL_GEN8                                     1

#define SPN_DEVICE_INTEL_SIMD8_LOG2                               3
#define SPN_DEVICE_INTEL_SIMD16_LOG2                              4
#define SPN_DEVICE_INTEL_SIMD32_LOG2                              5

#define SPN_DEVICE_MAX_PUSH_CONSTANTS_SIZE                        128 // bytes
#define SPN_DEVICE_SMEM_PER_SUBGROUP_DWORDS                       292

//
// TILE CONFIGURATION
//

#define SPN_DEVICE_TILE_WIDTH_LOG2                                3 // 8
#define SPN_DEVICE_TILE_HEIGHT_LOG2                               3 // 8

//
// BLOCK POOL CONFIGURATION
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_DEVICE_BLOCK_POOL_BLOCK_DWORDS_LOG2                   5
#define SPN_DEVICE_BLOCK_POOL_SUBBLOCK_DWORDS_LOG2                SPN_DEVICE_TILE_HEIGHT_LOG2

//
// KERNEL: GET STATUS
//

#define SPN_DEVICE_GET_STATUS_SUBGROUP_SIZE_LOG2                  0
#define SPN_DEVICE_GET_STATUS_WORKGROUP_SIZE                      2

//
// KERNEL: BLOCK POOL INIT
//

#define SPN_DEVICE_BLOCK_POOL_INIT_SUBGROUP_SIZE_LOG2             0
#define SPN_DEVICE_BLOCK_POOL_INIT_WORKGROUP_SIZE                 128

#define SPN_DEVICE_BLOCK_POOL_INIT_BP_IDS_PER_INVOCATION          16

//
// KERNEL: PATHS ALLOC
//
// Note that this workgroup only uses one lane but, depending on the
// target, it might be necessary to launch at least a subgroup.
//

#define SPN_DEVICE_PATHS_ALLOC_SUBGROUP_SIZE_LOG2                 0
#define SPN_DEVICE_PATHS_ALLOC_WORKGROUP_SIZE                     1

//
// KERNEL: PATHS COPY
//

#define SPN_DEVICE_PATHS_COPY_SUBGROUP_SIZE_LOG2                  SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_PATHS_COPY_WORKGROUP_SIZE                      ((1 << SPN_DEVICE_PATHS_COPY_SUBGROUP_SIZE_LOG2) * 1)

//
// KERNEL: FILLS SCAN
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_DEVICE_FILLS_SCAN_SUBGROUP_SIZE_LOG2                  SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_FILLS_SCAN_WORKGROUP_SIZE                      ((1 << SPN_DEVICE_FILLS_SCAN_SUBGROUP_SIZE_LOG2) * 1)

#define SPN_DEVICE_FILLS_SCAN_ROWS                                4
#define SPN_DEVICE_FILLS_SCAN_EXPAND()                            SPN_EXPAND_4()
#define SPN_DEVICE_FILLS_SCAN_EXPAND_I_LAST                       3

//
// KERNEL: FILLS EXPAND
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_DEVICE_FILLS_EXPAND_SUBGROUP_SIZE_LOG2                SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_FILLS_EXPAND_WORKGROUP_SIZE                    ((1 << SPN_DEVICE_FILLS_EXPAND_SUBGROUP_SIZE_LOG2) * 1)

//
// KERNEL: FILLS DISPATCH
//

#define SPN_DEVICE_FILLS_DISPATCH_SUBGROUP_SIZE_LOG2              SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_FILLS_DISPATCH_WORKGROUP_SIZE                  ((1 << SPN_DEVICE_FILLS_DISPATCH_SUBGROUP_SIZE_LOG2) * 1)

//
// KERNEL: RASTERIZE
//

// e.g. NVIDIA, AMD, Intel, ARM Bifrost, etc.
#define SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2                   SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE                       ((1 << SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2) * 1)

// can reduce this to force earlier launches of smaller grids
#define SPN_DEVICE_RASTERIZE_COHORT_SIZE                          (SPN_RASTER_COHORT_METAS_SIZE - 1)

//
// KERNEL: SEGMENT TTRK
//

// -- DETERMINED BY HOTSORT --

//
// KERNEL: RASTERS ALLOC
//

#define SPN_DEVICE_RASTERS_ALLOC_SUBGROUP_SIZE_LOG2               SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_RASTERS_ALLOC_WORKGROUP_SIZE                   ((1 << SPN_DEVICE_RASTERS_ALLOC_SUBGROUP_SIZE_LOG2) * 1)

//
// KERNEL: RASTERS PREFIX
//

#define SPN_DEVICE_RASTERS_PREFIX_SUBGROUP_SIZE_LOG2              SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_RASTERS_PREFIX_WORKGROUP_SIZE                  ((1 << SPN_DEVICE_RASTERS_PREFIX_SUBGROUP_SIZE_LOG2) * 1)

//
// KERNEL: PLACE TTPK & TTSK
//

#define SPN_DEVICE_PLACE_SUBGROUP_SIZE_LOG2                       SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_PLACE_WORKGROUP_SIZE                           ((1 << SPN_DEVICE_PLACE_SUBGROUP_SIZE_LOG2) * 1)

//
// KERNEL: SEGMENT TTCK
//

// -- DETERMINED BY HOTSORT --

//
// KERNEL: RENDER
//

#define SPN_DEVICE_RENDER_SUBGROUP_SIZE_LOG2                      SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_RENDER_WORKGROUP_SIZE                          ((1 << SPN_DEVICE_RENDER_SUBGROUP_SIZE_LOG2) * 1)

#define SPN_DEVICE_RENDER_LGF_USE_SHUFFLE
#define SPN_DEVICE_RENDER_TTCKS_USE_SHUFFLE
#define SPN_DEVICE_RENDER_STYLING_CMDS_USE_SHUFFLE

#define SPN_DEVICE_RENDER_TILE_CHANNEL_IS_FLOAT16                 // GEN8+ supports fp16x2

//
// NOTE(allanmac): Intel doesn't advertise support for the
// VK_FORMAT_FEATURE_IMAGE_STORAGE_BIT on a BGRA8 surface but will
// implicitly support this bit if the image is `writeonly`.
//

#define SPN_VK_GLSL_LAYOUT_IMAGE2D_FORMAT_IGNORED
#define SPN_DEVICE_RENDER_SURFACE_TYPE                            rgba8

//
// KERNEL: PATHS RECLAIM
//

#define SPN_DEVICE_PATHS_RECLAIM_SUBGROUP_SIZE_LOG2               SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_PATHS_RECLAIM_WORKGROUP_SIZE                   ((1 << SPN_DEVICE_PATHS_RECLAIM_SUBGROUP_SIZE_LOG2) * 1)

//
// KERNEL: RASTERS RECLAIM
//

#define SPN_DEVICE_RASTERS_RECLAIM_SUBGROUP_SIZE_LOG2             SPN_DEVICE_INTEL_SIMD8_LOG2
#define SPN_DEVICE_RASTERS_RECLAIM_WORKGROUP_SIZE                 ((1 << SPN_DEVICE_RASTERS_RECLAIM_SUBGROUP_SIZE_LOG2) * 1)

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_TARGETS_VENDORS_INTEL_GEN8_SPN_CONFIG_H_
