// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_LAYOUTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_LAYOUTS_H_

//
// FIXME(allanmac): unify the GLSL and VK binding macros
//

//
// clang-format off
//

#ifndef VULKAN          // defined by GLSL/VK compiler
#include "core_vk.h"    // c99/vk
#else
#include "core_glsl.h"  // glsl/vk
#endif

/////////////////////////////////////////////////////////////////
//
// PIPELINE EXPANSIONS
//
// *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
//
// NOTE: For now, pipelines are stored in alphabetical order
//
// NOTE: Always update "targets/spinel_comp_names.txt" to match
//
// *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
//
// NOTE: Not all kernels have push constants
//

#define SPN_VK_P_ID_BLOCK_POOL_INIT      block_pool_init
#define SPN_VK_P_ID_FILLS_DISPATCH       fills_dispatch
#define SPN_VK_P_ID_FILLS_EXPAND         fills_expand
#define SPN_VK_P_ID_FILLS_SCAN           fills_scan
#define SPN_VK_P_ID_GET_STATUS           get_status
#define SPN_VK_P_ID_PATHS_ALLOC          paths_alloc
#define SPN_VK_P_ID_PATHS_COPY           paths_copy
#define SPN_VK_P_ID_PATHS_RECLAIM        paths_reclaim
#define SPN_VK_P_ID_PLACE_TTPK           place_ttpk
#define SPN_VK_P_ID_PLACE_TTSK           place_ttsk
#define SPN_VK_P_ID_RASTERIZE_CUBIC      rasterize_cubic
#define SPN_VK_P_ID_RASTERIZE_LINE       rasterize_line
#define SPN_VK_P_ID_RASTERIZE_QUAD       rasterize_quad
#define SPN_VK_P_ID_RASTERIZE_RAT_CUBIC  rasterize_rat_cubic
#define SPN_VK_P_ID_RASTERIZE_RAT_QUAD   rasterize_rat_quad
#define SPN_VK_P_ID_RASTERS_ALLOC        rasters_alloc
#define SPN_VK_P_ID_RASTERS_PREFIX       rasters_prefix
#define SPN_VK_P_ID_RASTERS_RECLAIM      rasters_reclaim
#define SPN_VK_P_ID_RENDER               render
#define SPN_VK_P_ID_SEGMENT_TTCK         segment_ttck
#define SPN_VK_P_ID_SEGMENT_TTRK         segment_ttrk

#define SPN_VK_P_EXPAND()                                                                                \
  SPN_VK_P_EXPAND_X(0 ,SPN_VK_P_ID_BLOCK_POOL_INIT      ,SPN_VK_HOST_DECL_KERNEL_BLOCK_POOL_INIT())      \
  SPN_VK_P_EXPAND_X(1 ,SPN_VK_P_ID_FILLS_DISPATCH       ,SPN_VK_HOST_DECL_KERNEL_FILLS_DISPATCH())       \
  SPN_VK_P_EXPAND_X(2 ,SPN_VK_P_ID_FILLS_EXPAND         ,SPN_VK_HOST_DECL_KERNEL_FILLS_EXPAND())         \
  SPN_VK_P_EXPAND_X(3 ,SPN_VK_P_ID_FILLS_SCAN           ,SPN_VK_HOST_DECL_KERNEL_FILLS_SCAN())           \
  SPN_VK_P_EXPAND_X(4 ,SPN_VK_P_ID_GET_STATUS           ,SPN_VK_HOST_DECL_KERNEL_GET_STATUS())           \
  SPN_VK_P_EXPAND_X(5 ,SPN_VK_P_ID_PATHS_ALLOC          ,SPN_VK_HOST_DECL_KERNEL_PATHS_ALLOC())          \
  SPN_VK_P_EXPAND_X(6 ,SPN_VK_P_ID_PATHS_COPY           ,SPN_VK_HOST_DECL_KERNEL_PATHS_COPY())           \
  SPN_VK_P_EXPAND_X(7 ,SPN_VK_P_ID_PATHS_RECLAIM        ,SPN_VK_HOST_DECL_KERNEL_PATHS_RECLAIM())        \
  SPN_VK_P_EXPAND_X(8 ,SPN_VK_P_ID_PLACE_TTPK           ,SPN_VK_HOST_DECL_KERNEL_PLACE_TTPK())           \
  SPN_VK_P_EXPAND_X(9 ,SPN_VK_P_ID_PLACE_TTSK           ,SPN_VK_HOST_DECL_KERNEL_PLACE_TTSK())           \
  SPN_VK_P_EXPAND_X(10,SPN_VK_P_ID_RASTERIZE_CUBIC      ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_CUBIC())      \
  SPN_VK_P_EXPAND_X(11,SPN_VK_P_ID_RASTERIZE_LINE       ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_LINE())       \
  SPN_VK_P_EXPAND_X(12,SPN_VK_P_ID_RASTERIZE_QUAD       ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_QUAD())       \
  SPN_VK_P_EXPAND_X(13,SPN_VK_P_ID_RASTERIZE_RAT_CUBIC  ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_RAT_CUBIC())  \
  SPN_VK_P_EXPAND_X(14,SPN_VK_P_ID_RASTERIZE_RAT_QUAD   ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_RAT_QUAD())   \
  SPN_VK_P_EXPAND_X(15,SPN_VK_P_ID_RASTERS_ALLOC        ,SPN_VK_HOST_DECL_KERNEL_RASTERS_ALLOC())        \
  SPN_VK_P_EXPAND_X(16,SPN_VK_P_ID_RASTERS_PREFIX       ,SPN_VK_HOST_DECL_KERNEL_RASTERS_PREFIX())       \
  SPN_VK_P_EXPAND_X(17,SPN_VK_P_ID_RASTERS_RECLAIM      ,SPN_VK_HOST_DECL_KERNEL_RASTERS_RECLAIM())      \
  SPN_VK_P_EXPAND_X(18,SPN_VK_P_ID_RENDER               ,SPN_VK_HOST_DECL_KERNEL_RENDER())               \
  SPN_VK_P_EXPAND_X(19,SPN_VK_P_ID_SEGMENT_TTCK         ,SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTCK())         \
  SPN_VK_P_EXPAND_X(20,SPN_VK_P_ID_SEGMENT_TTRK         ,SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTRK())

#define SPN_VK_P_COUNT  21  // this is validated with a static assert

/////////////////////////////////////////////////////////////////
//
// DESCRIPTOR SET EXPANSIONS
//

#define SPN_VK_DS_ID_STATUS          status
#define SPN_VK_DS_ID_BLOCK_POOL      block_pool
#define SPN_VK_DS_ID_PATHS_COPY      paths_copy
#define SPN_VK_DS_ID_RASTERIZE       rasterize
#define SPN_VK_DS_ID_TTRKS           ttrks
#define SPN_VK_DS_ID_RASTER_IDS      raster_ids
#define SPN_VK_DS_ID_TTCKS           ttcks
#define SPN_VK_DS_ID_PLACE           place
#define SPN_VK_DS_ID_STYLING         styling
#define SPN_VK_DS_ID_SURFACE         surface
#define SPN_VK_DS_ID_RECLAIM         reclaim

#define SPN_VK_GLSL_DS_EXPAND()                                                                        \
  SPN_VK_GLSL_DS_STATUS    (SPN_EMPTY,SPN_EMPTY);                                                      \
  SPN_VK_GLSL_DS_BLOCK_POOL(SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                        \
  SPN_VK_GLSL_DS_PATHS_COPY(SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                                  \
  SPN_VK_GLSL_DS_RASTERIZE (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);    \
  SPN_VK_GLSL_DS_TTRKS     (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                                  \
  SPN_VK_GLSL_DS_RASTER_IDS(SPN_EMPTY,SPN_EMPTY);                                                      \
  SPN_VK_GLSL_DS_TTCKS     (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                                            \
  SPN_VK_GLSL_DS_PLACE     (SPN_EMPTY,SPN_EMPTY);                                                      \
  SPN_VK_GLSL_DS_STYLING   (SPN_EMPTY,SPN_EMPTY);                                                      \
  SPN_VK_GLSL_DS_SURFACE   (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                                            \
  SPN_VK_GLSL_DS_RECLAIM   (SPN_EMPTY,SPN_EMPTY);

#define SPN_VK_DS_EXPAND()                                                            \
  SPN_VK_DS_EXPAND_X(0 ,SPN_VK_DS_ID_STATUS,          SPN_VK_DS_STATUS())             \
  SPN_VK_DS_EXPAND_X(1 ,SPN_VK_DS_ID_BLOCK_POOL,      SPN_VK_DS_BLOCK_POOL())         \
  SPN_VK_DS_EXPAND_X(2 ,SPN_VK_DS_ID_PATHS_COPY,      SPN_VK_DS_PATHS_COPY())         \
  SPN_VK_DS_EXPAND_X(3 ,SPN_VK_DS_ID_RASTERIZE,       SPN_VK_DS_RASTERIZE())          \
  SPN_VK_DS_EXPAND_X(4 ,SPN_VK_DS_ID_TTRKS,           SPN_VK_DS_TTRKS())              \
  SPN_VK_DS_EXPAND_X(5 ,SPN_VK_DS_ID_RASTER_IDS,      SPN_VK_DS_RASTER_IDS())         \
  SPN_VK_DS_EXPAND_X(6 ,SPN_VK_DS_ID_TTCKS,           SPN_VK_DS_TTCKS())              \
  SPN_VK_DS_EXPAND_X(7 ,SPN_VK_DS_ID_PLACE,           SPN_VK_DS_PLACE())              \
  SPN_VK_DS_EXPAND_X(8 ,SPN_VK_DS_ID_STYLING,         SPN_VK_DS_STYLING())            \
  SPN_VK_DS_EXPAND_X(9 ,SPN_VK_DS_ID_SURFACE,         SPN_VK_DS_SURFACE())            \
  SPN_VK_DS_EXPAND_X(10,SPN_VK_DS_ID_RECLAIM,         SPN_VK_DS_RECLAIM())

#define SPN_VK_DS_COUNT  11  // this is validated with a static assert

//
// DESCRIPTOR SET: STATUS
//

#define SPN_VK_BINDING_STATUS_BP_ATOMICS  0

#define SPN_VK_DS_STATUS()                                              \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_STATUS, SPN_VK_BINDING_STATUS_BP_ATOMICS, status)

#define SPN_VK_GLSL_DS_STATUS(idx,mq_status)                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_STATUS,idx,                    \
                            SPN_VK_BINDING_STATUS_BP_ATOMICS,status) {  \
    mq_status SPN_MEMBER_FARRAY_UINT(status_bp_atomics,2);              \
  };

//
// DESCRIPTOR: DEBUG
//
// This is our only means of debugging compute shaders!
//

#ifndef NDEBUG

#define SPN_BP_DEBUG

#endif

//
//
//

#ifdef SPN_BP_DEBUG

#define SPN_VK_DESC_DEBUG()                                     \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,      \
                                  SPN_VK_BINDING_BP_DEBUG,      \
                                  bp_debug)

#define SPN_VK_GLSL_DEBUG(idx)                                                  \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_DEBUG,bp_debug) {                 \
    SPN_MEMBER_FARRAY_UINT(bp_debug_count,1);                                   \
    SPN_VK_GLSL_ALIGN_GPU_SEGMENT()                                             \
    SPN_MEMBER_VARRAY_UINT(bp_debug);                                           \
  };

#else

#define SPN_VK_DESC_DEBUG()
#define SPN_VK_GLSL_DEBUG(idx)

#endif

//
// DESCRIPTOR SET: BLOCK POOL
//
// Includes the block pool as well as the host map.
//
// Note the uint[] block pool is aliased with a uvec4[].
//

#define SPN_VK_BINDING_BP_IDS         0
#define SPN_VK_BINDING_BP_BLOCKS      1
#define SPN_VK_BINDING_BP_HOST_MAP    2
#define SPN_VK_BINDING_BP_DEBUG       3

#define SPN_VK_DS_BLOCK_POOL()                                                                        \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_BLOCK_POOL, SPN_VK_BINDING_BP_IDS,      bp_ids)        \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_BLOCK_POOL, SPN_VK_BINDING_BP_BLOCKS,   bp_blocks)     \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_BLOCK_POOL, SPN_VK_BINDING_BP_HOST_MAP, bp_host_map)   \
  SPN_VK_DESC_DEBUG()


#define SPN_VK_GLSL_DS_BLOCK_POOL(idx,mq_atomics,mq_ids,mq_blocks,mq_host_map)  \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_IDS,bp_ids) {                     \
    mq_atomics  SPN_MEMBER_FARRAY_UINT(bp_atomics,2);                           \
    SPN_VK_GLSL_ALIGN_GPU_SEGMENT()                                             \
    mq_ids      SPN_MEMBER_VARRAY_UINT(bp_ids);                                 \
  };                                                                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_BLOCKS,bp_blocks) {               \
    mq_blocks   SPN_MEMBER_VARRAY_UINT(bp_blocks);                              \
  };                                                                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_HOST_MAP,bp_host_map) {           \
    mq_host_map SPN_MEMBER_VARRAY_UINT(bp_host_map);                            \
  };                                                                            \
  SPN_VK_GLSL_DEBUG(idx)


#define SPN_VK_GLSL_DS_BLOCK_POOL_UVEC4(idx,mq_atomics,mq_ids,mq_blocks,mq_host_map) \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                             \
                            SPN_VK_BINDING_BP_IDS,bp_ids) {                          \
    mq_atomics  SPN_MEMBER_FARRAY_UINT(bp_atomics,2);                                \
    SPN_VK_GLSL_ALIGN_GPU_SEGMENT()                                                  \
    mq_ids      SPN_MEMBER_VARRAY_UINT(bp_ids);                                      \
  };                                                                                 \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                             \
                            SPN_VK_BINDING_BP_BLOCKS,bp_blocks_uvec4) {              \
    mq_blocks   SPN_MEMBER_VARRAY_UVEC4(bp_blocks_uvec4);                            \
  };                                                                                 \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                             \
                            SPN_VK_BINDING_BP_HOST_MAP,bp_host_map) {                \
    mq_host_map SPN_MEMBER_VARRAY_UINT(bp_host_map);                                 \
  };                                                                                 \
  SPN_VK_GLSL_DEBUG(idx)

//
// DESCRIPTOR SET: PATHS COPY
//
// Implemented as a ring buffer.
//

#define SPN_VK_BINDING_PC_ALLOC       0
#define SPN_VK_BINDING_PC_RING        1

#define SPN_VK_DS_PATHS_COPY()                                                                       \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_PATHS_COPY, SPN_VK_BINDING_PC_ALLOC, pc_alloc)        \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_PATHS_COPY, SPN_VK_BINDING_PC_RING,  pc_ring)

#define SPN_VK_GLSL_DS_PATHS_COPY(idx,mq_alloc,mq_cmds,mq_blocks)       \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_PATHS_COPY,idx,                \
                            SPN_VK_BINDING_PC_ALLOC,pc_alloc) {         \
    mq_alloc  SPN_MEMBER_VARRAY_UINT(pc_alloc);                         \
  };                                                                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_PATHS_COPY,idx,                \
                            SPN_VK_BINDING_PC_RING,pc_ring) {           \
    mq_cmds   SPN_MEMBER_VARRAY_UINT(pc_ring);                          \
  };

//
// DESCRIPTOR SET: RASTERIZE
//

#define SPN_VK_BINDING_FILL_CMDS      0
#define SPN_VK_BINDING_FILL_QUADS     1
#define SPN_VK_BINDING_FILL_SCAN      2
#define SPN_VK_BINDING_RAST_CMDS      3

#define SPN_VK_DS_RASTERIZE()                                                                      \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_FILL_CMDS,  fill_cmds)    \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_FILL_QUADS, fill_quads)   \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_FILL_SCAN,  fill_scan)    \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_RAST_CMDS,  rast_cmds)

#define SPN_VK_GLSL_DS_RASTERIZE(idx,                                                                   \
                                 mq_fill_cmds,mq_fill_quads,                                            \
                                 mq_fill_scan_dispatch,mq_fill_scan_counts,mq_fill_scan_prefix,         \
                                 mq_rast_cmds)                                                          \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_FILL_CMDS,fill_cmds) {                                       \
    mq_fill_cmds          SPN_MEMBER_VARRAY_UVEC4(fill_cmds);                                           \
  };                                                                                                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_FILL_QUADS,fill_quads) {                                     \
    mq_fill_quads         SPN_MEMBER_VARRAY_VEC4(fill_quads);                                           \
  };                                                                                                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_FILL_SCAN,fill_scan) {                                       \
    mq_fill_scan_dispatch SPN_MEMBER_FARRAY_UVEC4(fill_scan_dispatch,SPN_RAST_TYPE_COUNT);              \
    mq_fill_scan_counts   SPN_MEMBER_FARRAY_UINT(fill_scan_counts,SPN_RAST_TYPE_COUNT);                 \
    SPN_VK_GLSL_ALIGN_GPU_SEGMENT()                                                                     \
    mq_fill_scan_prefix   SPN_MEMBER_VARRAY_UVEC4(fill_scan_prefix);                                    \
  };                                                                                                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_RAST_CMDS,rast_cmds) {                                       \
    mq_rast_cmds          SPN_MEMBER_VARRAY_UVEC4(rast_cmds);                                           \
  };

//
// DESCRIPTOR SET: TTRKS
//
#define SPN_VK_BINDING_TTRKS          0

#define SPN_VK_DS_TTRKS()                                               \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_TTRKS, SPN_VK_BINDING_TTRKS, ttrks)

#define SPN_VK_GLSL_DS_TTRKS(idx,mq_ttrks_meta,mq_ttrks_count,mq_ttrks_keys)                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_TTRKS,idx,                                             \
                            SPN_VK_BINDING_TTRKS,ttrks) {                                       \
    mq_ttrks_meta    SPN_MEMBER_STRUCT(spn_rc_meta,ttrks_meta);                                 \
    mq_ttrks_count   SPN_MEMBER_UINT(ttrks_count);                                              \
    SPN_VK_GLSL_ALIGN_GPU_SEGMENT()                                                             \
    mq_ttrks_keys    SPN_MEMBER_VARRAY_UVEC2(ttrks_keys);                                       \
  };

//
// DESCRIPTOR SET: RASTER_IDS
//
#define SPN_VK_BINDING_RASTER_IDS     0

#define SPN_VK_DS_RASTER_IDS()                                  \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTER_IDS,      \
                                  SPN_VK_BINDING_RASTER_IDS,    \
                                  raster_ids)

#define SPN_VK_GLSL_DS_RASTER_IDS(idx,mq_raster_ids)                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTER_IDS,idx,                \
                            SPN_VK_BINDING_RASTER_IDS,raster_ids) {     \
    mq_raster_ids  SPN_MEMBER_VARRAY_UINT(raster_ids);                  \
  };                                                                    \

//
// DESCRIPTOR SET: TTCKS
//

#define SPN_VK_BINDING_TTCKS          0

#define SPN_VK_DS_TTCKS()                                               \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_TTCKS, SPN_VK_BINDING_TTCKS, ttcks)

#define SPN_VK_GLSL_DS_TTCKS(idx,mq_keys,mq_offsets)                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_TTCKS,idx,                             \
                            SPN_VK_BINDING_TTCKS,ttcks) {                       \
    mq_keys    SPN_MEMBER_FARRAY_UINT(ttcks_count,4);                           \
    mq_offsets SPN_MEMBER_FARRAY_UINT(offsets_count,4);                         \
    SPN_VK_GLSL_ALIGN_GPU_SEGMENT()                                             \
    mq_offsets SPN_MEMBER_FARRAY_UINT(offsets,(1<<SPN_TTCK_HI_BITS_XY));        \
    mq_keys    SPN_MEMBER_VARRAY_UVEC2(ttcks_keys);                             \
  };

//
// DESCRIPTOR SET: PLACE COMMANDS
//
// Implemented as a ring buffer.
//

#define SPN_VK_BINDING_PLACE          0

#define SPN_VK_DS_PLACE()                                               \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_PLACE, SPN_VK_BINDING_PLACE, place)

#define SPN_VK_GLSL_DS_PLACE(idx,mq_cmds)                       \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_PLACE,idx,             \
                            SPN_VK_BINDING_PLACE,cmds) {        \
    mq_cmds SPN_MEMBER_VARRAY_STRUCT(spn_cmd_place,cmds);       \
  };

//
// DESCRIPTOR SET: STYLING
//

#define SPN_VK_BINDING_STYLING        0

#define SPN_VK_DS_STYLING()                                             \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_STYLING, SPN_VK_BINDING_STYLING, styling)

#define SPN_VK_GLSL_DS_STYLING(idx,mq_styling)                  \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_STYLING,idx,           \
                            SPN_VK_BINDING_STYLING,styling)  {  \
    mq_styling SPN_MEMBER_VARRAY_UINT(styling);                 \
  };

//
// DESCRIPTOR SET: SURFACE
//

#define SPN_VK_BINDING_SURFACE        0 // STORAGE_IMAGE

#define SPN_VK_DS_SURFACE()                                             \
  SPN_VK_DESC_TYPE_STORAGE_IMAGE(SPN_VK_DS_ID_SURFACE, SPN_VK_BINDING_SURFACE, surface)

#define SPN_VK_GLSL_DS_SURFACE(idx,mq_surface,surface_type)     \
  SPN_VK_GLSL_LAYOUT_IMAGE2D(SPN_VK_DS_ID_SURFACE,idx,          \
                             SPN_VK_BINDING_SURFACE,            \
                             surface_type,surface);

//
// DESCRIPTOR SET: RECLAIM
//

#define SPN_VK_BINDING_RECLAIM        0

#define SPN_VK_DS_RECLAIM()                                             \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RECLAIM, SPN_VK_BINDING_RECLAIM, reclaim)

#define SPN_VK_GLSL_DS_RECLAIM(idx,mq_ring)                     \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RECLAIM,idx,           \
                            SPN_VK_BINDING_RECLAIM,ring) {      \
    mq_ring SPN_MEMBER_VARRAY_UINT(ring);                       \
  };

/////////////////////////////////////////////////////////////////
//
// GLSL COMPUTE SHADER BINDINGS
//

//
// KERNEL: GET STATUS
//

#define SPN_VK_GLSL_PUSH_KERNEL_GET_STATUS()

#define SPN_VK_GLSL_DECL_KERNEL_GET_STATUS()                            \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,noaccess,noaccess,noaccess);    \
  SPN_VK_GLSL_DS_STATUS(1,writeonly);

#define SPN_VK_HOST_DECL_KERNEL_GET_STATUS()                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_GET_STATUS,0,SPN_VK_DS_ID_BLOCK_POOL)      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_GET_STATUS,1,SPN_VK_DS_ID_STATUS)

//
// KERNEL: BLOCK POOL INIT
//

#define SPN_VK_GLSL_PUSH_KERNEL_BLOCK_POOL_INIT()       \
  SPN_VK_PUSH_UINT(bp_size)

#define SPN_VK_GLSL_DECL_KERNEL_BLOCK_POOL_INIT()                       \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,writeonly,writeonly,writeonly); \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_BLOCK_POOL_INIT());

#define SPN_VK_HOST_DECL_KERNEL_BLOCK_POOL_INIT()                                               \
  SPN_VK_HOST_DS(SPN_VK_P_ID_BLOCK_POOL_INIT,0,SPN_VK_DS_ID_BLOCK_POOL)                         \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_BLOCK_POOL_INIT,SPN_VK_GLSL_PUSH_KERNEL_BLOCK_POOL_INIT())

//
// KERNEL: PATHS ALLOC
//
// Note that this workgroup only uses one lane but, depending on the
// target, it might be necessary to launch at least a subgroup.
//
#define SPN_VK_GLSL_PUSH_KERNEL_PATHS_ALLOC()   \
  SPN_VK_PUSH_UINT(bp_mask)                     \
  SPN_VK_PUSH_UINT(pc_alloc_idx)                \
  SPN_VK_PUSH_UINT(pc_span)                     \
  SPN_VK_PUSH_UINT(pc_head)                     \
  SPN_VK_PUSH_UINT(pc_rolling)                  \
  SPN_VK_PUSH_UINT(pc_size)

#define SPN_VK_GLSL_DECL_KERNEL_PATHS_ALLOC()                           \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,readonly,writeonly,writeonly);  \
  SPN_VK_GLSL_DS_PATHS_COPY(1,writeonly,noaccess,noaccess);             \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_PATHS_ALLOC());

#define SPN_VK_HOST_DECL_KERNEL_PATHS_ALLOC()                                         \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PATHS_ALLOC,0,SPN_VK_DS_ID_BLOCK_POOL)                   \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PATHS_ALLOC,1,SPN_VK_DS_ID_PATHS_COPY)                   \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_PATHS_ALLOC,SPN_VK_GLSL_PUSH_KERNEL_PATHS_ALLOC())

//
// KERNEL: PATHS COPY
//
#define SPN_VK_GLSL_PUSH_KERNEL_PATHS_COPY()    \
  SPN_VK_GLSL_PUSH_KERNEL_PATHS_ALLOC()

#define SPN_VK_GLSL_DECL_KERNEL_PATHS_COPY()                              \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readonly,readonly,writeonly,writeonly);     \
  SPN_VK_GLSL_DS_PATHS_COPY(1,readonly,readonly,readonly);                \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_PATHS_COPY());

#define SPN_VK_HOST_DECL_KERNEL_PATHS_COPY()                                        \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PATHS_COPY,0,SPN_VK_DS_ID_BLOCK_POOL)                  \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PATHS_COPY,1,SPN_VK_DS_ID_PATHS_COPY)                  \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_PATHS_COPY,SPN_VK_GLSL_PUSH_KERNEL_PATHS_COPY())

//
// KERNEL: FILLS SCAN
//

#define SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN()      \
  SPN_VK_PUSH_UINT(bp_mask)                       \
  SPN_VK_PUSH_UINT(cmd_span)                      \
  SPN_VK_PUSH_UINT(cmd_head)                      \
  SPN_VK_PUSH_UINT(cmd_size)


#define SPN_VK_GLSL_DECL_KERNEL_FILLS_SCAN()                              \
  SPN_VK_GLSL_DS_BLOCK_POOL_UVEC4(0,readonly,readonly,readonly,readonly); \
  SPN_VK_GLSL_DS_RASTERIZE       (1,                                      \
                                  readonly,noaccess,                      \
                                  noaccess,readwrite,writeonly,           \
                                  noaccess);                              \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN());

#define SPN_VK_HOST_DECL_KERNEL_FILLS_SCAN()                                          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_SCAN,0,SPN_VK_DS_ID_BLOCK_POOL)                    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_SCAN,1,SPN_VK_DS_ID_RASTERIZE)                     \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_FILLS_SCAN,SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN())

//
// KERNEL: FILLS DISPATCH
//
// Compatible with FILLS SCAN
//
// Note: push constants are ignored
// Note: descriptor set 0 is ignored
//

#define SPN_VK_GLSL_PUSH_KERNEL_FILLS_DISPATCH()        \
  SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN()

#define SPN_VK_GLSL_DECL_KERNEL_FILLS_DISPATCH()                      \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,noaccess,noaccess);   \
  SPN_VK_GLSL_DS_RASTERIZE (1,                                        \
                            noaccess,noaccess,                        \
                            writeonly,readonly,noaccess,              \
                            noaccess);                                \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_FILLS_DISPATCH());

#define SPN_VK_HOST_DECL_KERNEL_FILLS_DISPATCH()                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_DISPATCH,0,SPN_VK_DS_ID_BLOCK_POOL)      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_DISPATCH,1,SPN_VK_DS_ID_RASTERIZE)       \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_FILLS_DISPATCH,SPN_VK_GLSL_PUSH_KERNEL_FILLS_DISPATCH())

//
// KERNEL: FILLS EXPAND
//
// Compatible with FILLS SCAN
//

#define SPN_VK_GLSL_PUSH_KERNEL_FILLS_EXPAND()      \
  SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN()

#define SPN_VK_GLSL_DECL_KERNEL_FILLS_EXPAND()                        \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readonly,readonly,readonly,readonly);   \
  SPN_VK_GLSL_DS_RASTERIZE (1,                                        \
                            readonly,noaccess,                        \
                            readonly,readonly,readonly,               \
                            writeonly);                               \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_FILLS_EXPAND());

#define SPN_VK_HOST_DECL_KERNEL_FILLS_EXPAND()                                      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_EXPAND,0,SPN_VK_DS_ID_BLOCK_POOL)                \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_EXPAND,1,SPN_VK_DS_ID_RASTERIZE)                 \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_FILLS_EXPAND,SPN_VK_GLSL_PUSH_KERNEL_FILLS_EXPAND())

//
// KERNEL: RASTERIZE_XXX
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()    \
  SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()                         \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,readwrite,readwrite,noaccess);  \
  SPN_VK_GLSL_DS_RASTERIZE (1,                                          \
                            noaccess,readonly,                          \
                            noaccess,noaccess,noaccess,                 \
                            readonly);                                  \
  SPN_VK_GLSL_DS_TTRKS     (2,noaccess,readwrite,writeonly);            \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX());

//
// KERNEL: RASTERIZE_CUBIC
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_CUBIC()   \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_CUBIC()   \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_CUBIC()                                         \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_CUBIC,0,SPN_VK_DS_ID_BLOCK_POOL)                   \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_CUBIC,1,SPN_VK_DS_ID_RASTERIZE)                    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_CUBIC,2,SPN_VK_DS_ID_TTRKS)                        \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_CUBIC,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_CUBIC())

//
// KERNEL: RASTERIZE_LINE
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_LINE()    \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_LINE()    \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_LINE()                                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_LINE,0,SPN_VK_DS_ID_BLOCK_POOL)                      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_LINE,1,SPN_VK_DS_ID_RASTERIZE)                       \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_LINE,2,SPN_VK_DS_ID_TTRKS)                           \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_LINE,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_LINE())

//
// KERNEL: RASTERIZE_QUAD
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_QUAD()    \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_QUAD()    \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_QUAD()                                          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_QUAD,0,SPN_VK_DS_ID_BLOCK_POOL)                    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_QUAD,1,SPN_VK_DS_ID_RASTERIZE)                     \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_QUAD,2,SPN_VK_DS_ID_TTRKS)                         \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_QUAD,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_QUAD())

//
// KERNEL: RASTERIZE_RAT_CUBIC
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_RAT_CUBIC()       \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_RAT_CUBIC()       \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_RAT_CUBIC()                                             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_CUBIC,0,SPN_VK_DS_ID_BLOCK_POOL)                       \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_CUBIC,1,SPN_VK_DS_ID_RASTERIZE)                        \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_CUBIC,2,SPN_VK_DS_ID_TTRKS)                            \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_RAT_CUBIC,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_RAT_CUBIC())

//
// KERNEL: RASTERIZE_RAT_QUAD
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_RAT_QUAD()        \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_RAT_QUAD()        \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_RAT_QUAD()                                              \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_QUAD,0,SPN_VK_DS_ID_BLOCK_POOL)                        \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_QUAD,1,SPN_VK_DS_ID_RASTERIZE)                         \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_QUAD,2,SPN_VK_DS_ID_TTRKS)                             \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_RAT_QUAD,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_RAT_QUAD())

//
// KERNELS: SORT AFTER RASTERIZE
//

//
// KERNEL: SEGMENT TTRK
//
// This kernel defines its own layout-compatible but arch-specific descriptor in
// order to harmonize with the HotSort library.
//
// Note that the push constants aren't currently used by this shader but are
// necessary for pipeline compatibility because HotSort uses the same pipeline
// layout.
//

#define SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTRK()  \
  SPN_VK_PUSH_UINT(kv_offset_in)                \
  SPN_VK_PUSH_UINT(kv_offset_out)               \
  SPN_VK_PUSH_UINT(kv_count)                    \
  SPN_VK_PUSH_UINT(padding) // padding for pipeline layout compatibility

#define SPN_VK_GLSL_DECL_KERNEL_SEGMENT_TTRK()                                             \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,noaccess,noaccess);                        \
  SPN_VK_GLSL_DS_TTRKS     (1,readwrite,noaccess,readwrite);                               \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTRK());

#define SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTRK()                                             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTRK, 0, SPN_VK_DS_ID_BLOCK_POOL)                     \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTRK, 1, SPN_VK_DS_ID_TTRKS)                          \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_SEGMENT_TTRK,SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTRK())

//
// KERNEL: RASTERS ALLOC
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC() \
  SPN_VK_PUSH_UINT(bp_mask)                     \
  SPN_VK_PUSH_UINT(raster_span)                 \
  SPN_VK_PUSH_UINT(raster_head)                 \
  SPN_VK_PUSH_UINT(raster_size)

#define SPN_VK_GLSL_DECL_KERNEL_RASTERS_ALLOC()                             \
  SPN_VK_GLSL_DS_BLOCK_POOL_UVEC4(0,readwrite,readonly,noaccess,writeonly); \
  SPN_VK_GLSL_DS_TTRKS           (1,readwrite,noaccess,noaccess);           \
  SPN_VK_GLSL_DS_RASTER_IDS      (2,readonly);                              \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC());

#define SPN_VK_HOST_DECL_KERNEL_RASTERS_ALLOC()                                             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_ALLOC,0,SPN_VK_DS_ID_BLOCK_POOL)                       \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_ALLOC,1,SPN_VK_DS_ID_TTRKS)                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_ALLOC,2,SPN_VK_DS_ID_RASTER_IDS)                       \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERS_ALLOC,SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC())

//
// KERNEL: RASTERS PREFIX
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERS_PREFIX()    \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERS_PREFIX()                            \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readonly,readonly,readwrite,noaccess);        \
  SPN_VK_GLSL_DS_TTRKS     (1,readonly,readonly,readonly);                  \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERS_PREFIX());

#define SPN_VK_HOST_DECL_KERNEL_RASTERS_PREFIX()                                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_PREFIX,0,SPN_VK_DS_ID_BLOCK_POOL)                      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_PREFIX,1,SPN_VK_DS_ID_TTRKS)                           \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERS_PREFIX,SPN_VK_GLSL_PUSH_KERNEL_RASTERS_PREFIX())

//
// KERNEL: PLACE_TTPK
//

#define SPN_VK_GLSL_PUSH_KERNEL_PLACE_TTPK()  \
  SPN_VK_PUSH_IVEC4(place_clip)               \
  SPN_VK_PUSH_UINT(place_head)                \
  SPN_VK_PUSH_UINT(place_span)                \
  SPN_VK_PUSH_UINT(place_size)

#define SPN_VK_GLSL_DECL_KERNEL_PLACE_TTPK()                        \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,readonly,readonly); \
  SPN_VK_GLSL_DS_TTCKS     (1,readwrite,noaccess);                  \
  SPN_VK_GLSL_DS_PLACE     (2,readonly);                            \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_PLACE_TTPK());

#define SPN_VK_HOST_DECL_KERNEL_PLACE_TTPK()                                          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE_TTPK,0,SPN_VK_DS_ID_BLOCK_POOL)                    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE_TTPK,1,SPN_VK_DS_ID_TTCKS)                         \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE_TTPK,2,SPN_VK_DS_ID_PLACE)                         \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_PLACE_TTPK,SPN_VK_GLSL_PUSH_KERNEL_PLACE_TTPK())

//
// KERNEL: PLACE_TTSK
//

#define SPN_VK_GLSL_PUSH_KERNEL_PLACE_TTSK()  \
  SPN_VK_PUSH_IVEC4(place_clip)               \
  SPN_VK_PUSH_UINT(place_head)                \
  SPN_VK_PUSH_UINT(place_span)                \
  SPN_VK_PUSH_UINT(place_size)

#define SPN_VK_GLSL_DECL_KERNEL_PLACE_TTSK()                        \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,readonly,readonly); \
  SPN_VK_GLSL_DS_TTCKS     (1,readwrite,noaccess);                  \
  SPN_VK_GLSL_DS_PLACE     (2,readonly);                            \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_PLACE_TTSK());

#define SPN_VK_HOST_DECL_KERNEL_PLACE_TTSK()                                          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE_TTSK,0,SPN_VK_DS_ID_BLOCK_POOL)                    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE_TTSK,1,SPN_VK_DS_ID_TTCKS)                         \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE_TTSK,2,SPN_VK_DS_ID_PLACE)                         \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_PLACE_TTSK,SPN_VK_GLSL_PUSH_KERNEL_PLACE_TTSK())

//
// KERNEL: SEGMENT TTCK
//
// This kernel defines its own layout-compatible but arch-specific
// descriptor in order to harmonize with the HotSort library.
//
// Note that the push constants aren't currently used by this shader but are
// necessary for pipeline compatibility because HotSort uses the same pipeline
// layout.
//

#define SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTCK()   \
  SPN_VK_PUSH_UINT(kv_offset_in)                 \
  SPN_VK_PUSH_UINT(kv_offset_out)                \
  SPN_VK_PUSH_UINT(kv_count)                     \
  SPN_VK_PUSH_UINT(padding) // padding for pipeline layout compatibility

#define SPN_VK_GLSL_DECL_KERNEL_SEGMENT_TTCK()                          \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,noaccess,noaccess);     \
  SPN_VK_GLSL_DS_TTCKS(1,readonly,readwrite);                           \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTCK());

#define SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTCK()                                             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTCK,0,SPN_VK_DS_ID_BLOCK_POOL)                       \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTCK,1,SPN_VK_DS_ID_TTCKS)                            \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_SEGMENT_TTCK,SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTCK())

//
// KERNEL: RENDER
//

#define SPN_VK_GLSL_PUSH_KERNEL_RENDER()        \
  SPN_VK_PUSH_IVEC4(render_clip)

#define SPN_VK_GLSL_DECL_KERNEL_RENDER()                                    \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,readonly,readonly);         \
  SPN_VK_GLSL_DS_TTCKS     (1,readonly,readonly);                           \
  SPN_VK_GLSL_DS_STYLING   (2,readonly);                                    \
  SPN_VK_GLSL_DS_SURFACE   (3,writeonly,SPN_DEVICE_RENDER_SURFACE_TYPE);    \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RENDER());

#define SPN_VK_HOST_DECL_KERNEL_RENDER()                           \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,0,SPN_VK_DS_ID_BLOCK_POOL)     \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,1,SPN_VK_DS_ID_TTCKS)          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,2,SPN_VK_DS_ID_STYLING)        \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,3,SPN_VK_DS_ID_SURFACE)        \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RENDER,SPN_VK_GLSL_PUSH_KERNEL_RENDER())

//
// KERNEL: PATHS RECLAIM
//

#define SPN_VK_GLSL_PUSH_KERNEL_PATHS_RECLAIM()                         \
  SPN_VK_PUSH_UINT(bp_mask)                                             \
  SPN_VK_PUSH_UINT(ring_size)                                           \
  SPN_VK_PUSH_UINT(ring_head)                                           \
  SPN_VK_PUSH_UINT(ring_span)

#define SPN_VK_GLSL_DECL_KERNEL_PATHS_RECLAIM()                         \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,readwrite,readonly,readonly);   \
  SPN_VK_GLSL_DS_RECLAIM(1,readonly);                                   \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_PATHS_RECLAIM());

#define SPN_VK_HOST_DECL_KERNEL_PATHS_RECLAIM()                          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PATHS_RECLAIM,0,SPN_VK_DS_ID_BLOCK_POOL)    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PATHS_RECLAIM,1,SPN_VK_DS_ID_RECLAIM)       \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_PATHS_RECLAIM,SPN_VK_GLSL_PUSH_KERNEL_PATHS_RECLAIM())

//
// KERNEL: RASTERS RECLAIM
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERS_RECLAIM()                       \
  SPN_VK_PUSH_UINT(bp_mask)                                             \
  SPN_VK_PUSH_UINT(ring_size)                                           \
  SPN_VK_PUSH_UINT(ring_head)                                           \
  SPN_VK_PUSH_UINT(ring_span)

#define SPN_VK_GLSL_DECL_KERNEL_RASTERS_RECLAIM()                       \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,readwrite,readonly,readonly);   \
  SPN_VK_GLSL_DS_RECLAIM(1,readonly);                                   \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERS_RECLAIM());

#define SPN_VK_HOST_DECL_KERNEL_RASTERS_RECLAIM()                       \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_RECLAIM,0,SPN_VK_DS_ID_BLOCK_POOL) \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_RECLAIM,1,SPN_VK_DS_ID_RECLAIM)    \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERS_RECLAIM,SPN_VK_GLSL_PUSH_KERNEL_RASTERS_RECLAIM())

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_VK_LAYOUTS_H_
