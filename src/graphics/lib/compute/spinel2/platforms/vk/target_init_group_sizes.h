// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_INIT_GROUP_SIZES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_INIT_GROUP_SIZES_H_

//
// Convenience include for initializing the target's pipeline group sizes.
//
// This designated initialization sequence assumes the Spinel target's
// "config.h" has already been included.
//
// clang-format off
//

.named = {
  .block_pool_init = {
    .workgroup     = SPN_DEVICE_BLOCK_POOL_INIT_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_BLOCK_POOL_INIT_SUBGROUP_SIZE_LOG2
  },
  .fill_dispatch = {
    .workgroup     = SPN_DEVICE_FILL_DISPATCH_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_FILL_DISPATCH_SUBGROUP_SIZE_LOG2
  },
  .fill_expand = {
    .workgroup     = SPN_DEVICE_FILL_EXPAND_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_FILL_EXPAND_SUBGROUP_SIZE_LOG2
  },
  .fill_scan = {
    .workgroup     = SPN_DEVICE_FILL_SCAN_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_FILL_SCAN_SUBGROUP_SIZE_LOG2
  },
  .paths_alloc = {
    .workgroup     = SPN_DEVICE_PATHS_ALLOC_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_PATHS_ALLOC_SUBGROUP_SIZE_LOG2
  },
  .paths_copy = {
    .workgroup     = SPN_DEVICE_PATHS_COPY_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_PATHS_COPY_SUBGROUP_SIZE_LOG2
  },
  .paths_reclaim = {
    .workgroup     = SPN_DEVICE_PATHS_RECLAIM_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_PATHS_RECLAIM_SUBGROUP_SIZE_LOG2
  },
  .place_ttpk = {
    .workgroup     = SPN_DEVICE_PLACE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_PLACE_SUBGROUP_SIZE_LOG2
  },
  .place_ttsk = {
    .workgroup     = SPN_DEVICE_PLACE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_PLACE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_cubic = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_line = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_quad = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_proj_cubic = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_proj_line = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_proj_quad = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_rat_cubic = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasterize_rat_quad = {
    .workgroup     = SPN_DEVICE_RASTERIZE_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERIZE_SUBGROUP_SIZE_LOG2
  },
  .rasters_alloc = {
    .workgroup     = SPN_DEVICE_RASTERS_ALLOC_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERS_ALLOC_SUBGROUP_SIZE_LOG2
  },
  .rasters_prefix = {
    .workgroup     = SPN_DEVICE_RASTERS_PREFIX_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERS_PREFIX_SUBGROUP_SIZE_LOG2
  },
  .rasters_reclaim = {
    .workgroup     = SPN_DEVICE_RASTERS_RECLAIM_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RASTERS_RECLAIM_SUBGROUP_SIZE_LOG2
  },
  .render = {
    .workgroup     = SPN_DEVICE_RENDER_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RENDER_SUBGROUP_SIZE_LOG2
  },
  .render_dispatch = {
    .workgroup     = SPN_DEVICE_RENDER_DISPATCH_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_RENDER_DISPATCH_SUBGROUP_SIZE_LOG2
  },
  .ttcks_segment = {
    .workgroup     = SPN_DEVICE_TTCKS_SEGMENT_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_TTCKS_SEGMENT_SUBGROUP_SIZE_LOG2
  },
  .ttcks_segment_dispatch = {
    .workgroup     = SPN_DEVICE_TTCKS_SEGMENT_DISPATCH_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_TTCKS_SEGMENT_DISPATCH_SUBGROUP_SIZE_LOG2
  },
  .ttrks_segment = {
    .workgroup     = SPN_DEVICE_TTRKS_SEGMENT_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_TTRKS_SEGMENT_SUBGROUP_SIZE_LOG2
  },
  .ttrks_segment_dispatch = {
    .workgroup     = SPN_DEVICE_TTRKS_SEGMENT_DISPATCH_WORKGROUP_SIZE,
    .subgroup_log2 = SPN_DEVICE_TTRKS_SEGMENT_DISPATCH_SUBGROUP_SIZE_LOG2
  },
},

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TARGET_INIT_GROUP_SIZES_H_
