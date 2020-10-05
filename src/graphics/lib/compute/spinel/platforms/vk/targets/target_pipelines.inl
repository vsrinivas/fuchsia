// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Initialize the push sizes, workgroup sizes and subgroup sizes
//

//
// clang-format off
//

#undef  SPN_VK_PUSH_UINT
#undef  SPN_VK_PUSH_UVEC4
#undef  SPN_VK_PUSH_IVEC4
#undef  SPN_VK_PUSH_UINT_FARRAY
#undef  SPN_VK_PUSH_UINT_VARRAY

#define SPN_VK_PUSH_UINT(name)            + sizeof(SPN_TYPE_UINT)
#define SPN_VK_PUSH_UVEC4(name)           + sizeof(SPN_TYPE_UVEC4)
#define SPN_VK_PUSH_IVEC4(name)           + sizeof(SPN_TYPE_IVEC4)
#define SPN_VK_PUSH_UINT_FARRAY(name,len) + sizeof(SPN_TYPE_UINT) * len
#define SPN_VK_PUSH_UINT_VARRAY(name,len) + sizeof(SPN_TYPE_UINT) * len

#undef  SPN_VK_HOST_DS
#define SPN_VK_HOST_DS(_p_id,_ds_idx,_ds_id)

#undef  SPN_VK_HOST_PUSH
#define SPN_VK_HOST_PUSH(_p_id,_p_pc)  ._p_id = (0 _p_pc),

//
// clang-format on
//

.push_sizes = {
   .named = {
#undef SPN_VK_P_EXPAND_X
#define SPN_VK_P_EXPAND_X(_p_idx, _p_id, _p_descs) _p_descs

     SPN_VK_P_EXPAND()
   },
},

.group_sizes = {
   .named = {
     .block_pool_init = {
       .workgroup     = SPN_DEVICE_BLOCK_POOL_INIT_WORKGROUP_SIZE,
       .subgroup_log2 = SPN_DEVICE_BLOCK_POOL_INIT_SUBGROUP_SIZE_LOG2
     },
     .fills_dispatch = {
       .workgroup     = SPN_DEVICE_FILLS_DISPATCH_WORKGROUP_SIZE,
       .subgroup_log2 = SPN_DEVICE_FILLS_DISPATCH_SUBGROUP_SIZE_LOG2
     },
     .fills_expand = {
       .workgroup     = SPN_DEVICE_FILLS_EXPAND_WORKGROUP_SIZE,
       .subgroup_log2 = SPN_DEVICE_FILLS_EXPAND_SUBGROUP_SIZE_LOG2
     },
     .fills_scan = {
       .workgroup     = SPN_DEVICE_FILLS_SCAN_WORKGROUP_SIZE,
       .subgroup_log2 = SPN_DEVICE_FILLS_SCAN_SUBGROUP_SIZE_LOG2
     },
     .get_status = {
       .workgroup     = SPN_DEVICE_GET_STATUS_WORKGROUP_SIZE,
       .subgroup_log2 = SPN_DEVICE_GET_STATUS_SUBGROUP_SIZE_LOG2
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
     .segment_ttck = {
       .workgroup     = (1 << HS_SLAB_THREADS_LOG2),
       .subgroup_log2 = HS_SLAB_THREADS_LOG2
     },
     .segment_ttrk = {
       .workgroup     = (1 << HS_SLAB_THREADS_LOG2),
       .subgroup_log2 = HS_SLAB_THREADS_LOG2
     },
   },
},

//
//
//
