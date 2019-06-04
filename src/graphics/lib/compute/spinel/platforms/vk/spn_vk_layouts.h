// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPN_VK_LAYOUTS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPN_VK_LAYOUTS_H_

//
//
//

#ifndef VULKAN        // defined by GLSL/VK compiler
#include "core_vk.h"  // c99/vk
#else
#include "core_glsl.h"  // glsl/vk
#endif

//
// clang-format off
//

/////////////////////////////////////////////////////////////////
//
// PIPELINE EXPANSION
//
// *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
// NOTE: Always update "targets/spinel_comp_names.txt" to match
// *-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
//
// NOTE: For now, pipelines are stored in alphabetical order
//
// NOTE: Not all kernels have push constants
//

#define SPN_VK_P_ID_BLOCK_POOL_INIT     block_pool_init
#define SPN_VK_P_ID_FILLS_DISPATCH      fills_dispatch
#define SPN_VK_P_ID_FILLS_EXPAND        fills_expand
#define SPN_VK_P_ID_FILLS_SCAN          fills_scan
#define SPN_VK_P_ID_PATHS_ALLOC         paths_alloc
#define SPN_VK_P_ID_PATHS_COPY          paths_copy
#define SPN_VK_P_ID_PATHS_RECLAIM       paths_reclaim
#define SPN_VK_P_ID_PLACE               place
#define SPN_VK_P_ID_RASTERIZE_CUBIC     rasterize_cubic
#define SPN_VK_P_ID_RASTERIZE_LINE      rasterize_line
#define SPN_VK_P_ID_RASTERIZE_QUAD      rasterize_quad
#define SPN_VK_P_ID_RASTERIZE_RAT_CUBIC rasterize_rat_cubic
#define SPN_VK_P_ID_RASTERIZE_RAT_QUAD  rasterize_rat_quad
#define SPN_VK_P_ID_RASTERS_ALLOC       rasters_alloc
#define SPN_VK_P_ID_RASTERS_PREFIX      rasters_prefix
#define SPN_VK_P_ID_RASTERS_RECLAIM     rasters_reclaim
#define SPN_VK_P_ID_RENDER              render
#define SPN_VK_P_ID_SEGMENT_TTCK        segment_ttck
#define SPN_VK_P_ID_SEGMENT_TTRK        segment_ttrk

#define SPN_VK_P_EXPAND()                                                                             \
  SPN_VK_P_EXPAND_X(0 ,SPN_VK_P_ID_BLOCK_POOL_INIT    ,SPN_VK_HOST_DECL_KERNEL_BLOCK_POOL_INIT())     \
  SPN_VK_P_EXPAND_X(1 ,SPN_VK_P_ID_FILLS_DISPATCH     ,SPN_VK_HOST_DECL_KERNEL_FILLS_DISPATCH())      \
  SPN_VK_P_EXPAND_X(2 ,SPN_VK_P_ID_FILLS_EXPAND       ,SPN_VK_HOST_DECL_KERNEL_FILLS_EXPAND())        \
  SPN_VK_P_EXPAND_X(3 ,SPN_VK_P_ID_FILLS_SCAN         ,SPN_VK_HOST_DECL_KERNEL_FILLS_SCAN())          \
  SPN_VK_P_EXPAND_X(4 ,SPN_VK_P_ID_PATHS_ALLOC        ,SPN_VK_HOST_DECL_KERNEL_PATHS_ALLOC())         \
  SPN_VK_P_EXPAND_X(5 ,SPN_VK_P_ID_PATHS_COPY         ,SPN_VK_HOST_DECL_KERNEL_PATHS_COPY())          \
  SPN_VK_P_EXPAND_X(6 ,SPN_VK_P_ID_PATHS_RECLAIM      ,SPN_VK_HOST_DECL_KERNEL_PATHS_RECLAIM())       \
  SPN_VK_P_EXPAND_X(7 ,SPN_VK_P_ID_PLACE              ,SPN_VK_HOST_DECL_KERNEL_PLACE())               \
  SPN_VK_P_EXPAND_X(8 ,SPN_VK_P_ID_RASTERIZE_CUBIC    ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_CUBIC())     \
  SPN_VK_P_EXPAND_X(9 ,SPN_VK_P_ID_RASTERIZE_LINE     ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_LINE())      \
  SPN_VK_P_EXPAND_X(10,SPN_VK_P_ID_RASTERIZE_QUAD     ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_QUAD())      \
  SPN_VK_P_EXPAND_X(11,SPN_VK_P_ID_RASTERIZE_RAT_CUBIC,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_RAT_CUBIC()) \
  SPN_VK_P_EXPAND_X(12,SPN_VK_P_ID_RASTERIZE_RAT_QUAD ,SPN_VK_HOST_DECL_KERNEL_RASTERIZE_RAT_QUAD())  \
  SPN_VK_P_EXPAND_X(13,SPN_VK_P_ID_RASTERS_ALLOC      ,SPN_VK_HOST_DECL_KERNEL_RASTERS_ALLOC())       \
  SPN_VK_P_EXPAND_X(14,SPN_VK_P_ID_RASTERS_PREFIX     ,SPN_VK_HOST_DECL_KERNEL_RASTERS_PREFIX())      \
  SPN_VK_P_EXPAND_X(15,SPN_VK_P_ID_RASTERS_RECLAIM    ,SPN_VK_HOST_DECL_KERNEL_RASTERS_RECLAIM())     \
  SPN_VK_P_EXPAND_X(16,SPN_VK_P_ID_RENDER             ,SPN_VK_HOST_DECL_KERNEL_RENDER())              \
  SPN_VK_P_EXPAND_X(17,SPN_VK_P_ID_SEGMENT_TTCK       ,SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTCK())        \
  SPN_VK_P_EXPAND_X(18,SPN_VK_P_ID_SEGMENT_TTRK       ,SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTRK())

#define SPN_VK_P_COUNT  19  // this is validated elsewhere

/////////////////////////////////////////////////////////////////
//
// DESCRIPTOR EXPANSION
//

#define SPN_VK_DS_ID_BLOCK_POOL     block_pool
#define SPN_VK_DS_ID_PATHS_COPY     paths_copy
#define SPN_VK_DS_ID_RASTERIZE      rasterize      // fill/alloc/raster cmds
#define SPN_VK_DS_ID_RASTERIZE_POST rasterize_post // ttrks + metas
#define SPN_VK_DS_ID_TTCKS          ttcks
#define SPN_VK_DS_ID_PLACE          place
#define SPN_VK_DS_ID_STYLING        styling
#define SPN_VK_DS_ID_SURFACE        surface

#define SPN_VK_GLSL_DS_EXPAND()                                                                     \
  SPN_VK_GLSL_DS_BLOCK_POOL    (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                 \
  SPN_VK_GLSL_DS_PATHS_COPY    (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                           \
  SPN_VK_GLSL_DS_RASTERIZE     (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);       \
  SPN_VK_GLSL_DS_RASTERIZE_POST(SPN_EMPTY,SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                           \
  SPN_VK_GLSL_DS_TTCKS         (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);                                     \
  SPN_VK_GLSL_DS_PLACE         (SPN_EMPTY,SPN_EMPTY);                                               \
  SPN_VK_GLSL_DS_STYLING       (SPN_EMPTY,SPN_EMPTY);                                               \
  SPN_VK_GLSL_DS_SURFACE       (SPN_EMPTY,SPN_EMPTY,SPN_EMPTY);

#define SPN_VK_DS_EXPAND()                                                          \
  SPN_VK_DS_EXPAND_X(0 ,SPN_VK_DS_ID_BLOCK_POOL,    SPN_VK_DS_BLOCK_POOL())         \
  SPN_VK_DS_EXPAND_X(1 ,SPN_VK_DS_ID_PATHS_COPY,    SPN_VK_DS_PATHS_COPY())         \
  SPN_VK_DS_EXPAND_X(2 ,SPN_VK_DS_ID_RASTERIZE,     SPN_VK_DS_RASTERIZE())          \
  SPN_VK_DS_EXPAND_X(3 ,SPN_VK_DS_ID_RASTERIZE_POST,SPN_VK_DS_RASTERIZE_POST())     \
  SPN_VK_DS_EXPAND_X(4 ,SPN_VK_DS_ID_TTCKS,         SPN_VK_DS_TTCKS())              \
  SPN_VK_DS_EXPAND_X(5 ,SPN_VK_DS_ID_PLACE,         SPN_VK_DS_PLACE())              \
  SPN_VK_DS_EXPAND_X(6 ,SPN_VK_DS_ID_STYLING,       SPN_VK_DS_STYLING())            \
  SPN_VK_DS_EXPAND_X(7 ,SPN_VK_DS_ID_SURFACE,       SPN_VK_DS_SURFACE())

#define SPN_VK_DS_COUNT  8 // this is validated elsewhere

//
// DESCRIPTOR EXTENT TYPES
//
// Interactions between the host<>device occur through Spinel extents.
//
// An extent is a very lightweight abstraction that captures a
// specific location, lifetime and access pattern of one or more
// Vulkan resources.
//
// For now, the extent resources we're managing are limited to
// VkDeviceMemory and VkBuffer instances as well as mapped pointers.
//
// Legend:
//
//   P  :  durable
//   T  :  ephemeral
//   X  :  durable/ephemeral is target-specific
//   H  :  host
//   D  :  device
//   R  :  read
//   W  :  write
//   1  :  once -- e.g. w1 is 'write-once'
//   N  :  many -- e.g. rN is 'read-many'
//   G  :  ring
//   S  :  ring snapshot
//
// Examples:
//
//   PDRW        : permanent device-side read-write extent
//   PHW1G_TDRNS : permanent write-once ring with temporary device-side read-many snapshot
//
// NOTE: The extent acronym captures the *intent* but an
// implementation may be target-specific.
//
// For example, a PHW1G_TDR1S ring buffer is implemented based on the
// target's capabilities, resource limitations and observed
// performance.  Here are 4 possible implementations:
//
//   - a write-through to a perm device buffer                  (AMD)          -- update bufferinfo
//   - a copy from the perm host buffer to a perm device buffer (dGPU)         -- copy / update bufferinfo
//   - a copy from the perm host buffer to a temp device buffer (dGPU)         -- suballocate temp buffer+id / copy / update bufferinfo
//   - a device-readable perm host coherent buffer              (iGPU or dGPU) -- update bufferinfo
//

//
// Spinel allocation type
//
// FIXME -- MOVE EXTENT HINTS INTO THE TARGET CONFIG
//

#define SPN_VK_ALLOC_PERM_BIT  (1u<<31)
#define SPN_VK_ALLOC_TEMP_BIT  (1u<<30)

// (SPN_VK_TEMP | copy to DEVICE_LOCAL)         or
// (SPN_VK_PERM | HOST_VISIBLE | HOST_COHERENT) or
// (SPN_VK_PERM | DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT)

//
// Current implementation is limited to the extents below.
//

#if 0
#define SPN_VK_EXTENT_PDRW
#define SPN_VK_EXTENT_TDRW
#define SPN_VK_EXTENT_PHWN_PDRN
#define SPN_VK_EXTENT_PHW1G_TDR1S
#define SPN_VK_EXTENT_PHW1G_TDRNS
#define SPN_VK_EXTENT_IMAGE
#endif

//
// DESCRIPTOR: BLOCK POOL
//
// Includes the block pool as well as the host map.
//
// Note the uint[] block pool is aliased with a uvec4[].
//

#define SPN_VK_BINDING_BP_IDS         0
#define SPN_VK_BINDING_BP_BLOCKS      1
#define SPN_VK_BINDING_BP_HOST_MAP    2

#define SPN_VK_DS_BLOCK_POOL()                                                                                                \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_BLOCK_POOL, SPN_VK_BINDING_BP_IDS,      SPN_VK_EXTENT_PDRW, bp_ids)            \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_BLOCK_POOL, SPN_VK_BINDING_BP_BLOCKS,   SPN_VK_EXTENT_PDRW, bp_blocks)         \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_BLOCK_POOL, SPN_VK_BINDING_BP_HOST_MAP, SPN_VK_EXTENT_PDRW, bp_host_map)

#define SPN_VK_GLSL_DS_BLOCK_POOL(idx,mq_atomics,mq_ids,mq_blocks,mq_host_map)  \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_IDS,bp_ids) {                     \
    mq_atomics  SPN_MEMBER_FARRAY_UINT(bp_atomics,2);                           \
    SPN_VK_GLSL_ALIGN()                                                         \
    mq_ids      SPN_MEMBER_VARRAY_UINT(bp_ids);                                 \
  };                                                                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_BLOCKS,bp_blocks) {               \
    mq_blocks   SPN_MEMBER_VARRAY_UINT(bp_blocks);                              \
  };                                                                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_BLOCKS,bp_blocks_uvec4) {         \
    mq_blocks   SPN_MEMBER_VARRAY_UVEC4(bp_blocks_uvec4);                       \
  };                                                                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_BLOCK_POOL,idx,                        \
                            SPN_VK_BINDING_BP_HOST_MAP,bp_host_map) {           \
    mq_host_map SPN_MEMBER_VARRAY_UINT(bp_host_map);                            \
  };

//
// DESCRIPTOR: PATHS COPY
//
// Implemented as a ring buffer.
//

#define SPN_VK_BINDING_PC_ALLOC       0
#define SPN_VK_BINDING_PC_RING        1

#define SPN_VK_DS_PATHS_COPY()                                                                                                  \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_PATHS_COPY, SPN_VK_BINDING_PC_ALLOC, SPN_VK_EXTENT_PDRW,        pc_alloc)        \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_PATHS_COPY, SPN_VK_BINDING_PC_RING,  SPN_VK_EXTENT_PHW1G_TDR1S, pc_ring)

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
// DESCRIPTOR: RASTERIZE
//

#define SPN_VK_BINDING_FILL_CMDS      0
#define SPN_VK_BINDING_FILL_QUADS     1
#define SPN_VK_BINDING_FILL_SCAN      2
#define SPN_VK_BINDING_RAST_CMDS      3

#define SPN_VK_DS_RASTERIZE()                                                                                                 \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_FILL_CMDS,  SPN_VK_EXTENT_PHW1G_TDRNS, fill_cmds)    \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_FILL_QUADS, SPN_VK_EXTENT_PHW1G_TDRNS, fill_quads)   \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_FILL_SCAN,  SPN_VK_EXTENT_TDRW,        fill_scan)    \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE, SPN_VK_BINDING_RAST_CMDS,  SPN_VK_EXTENT_TDRW,        rast_cmds)

#define SPN_VK_GLSL_DS_RASTERIZE(idx,                                                                   \
                                 mq_fill_cmds,mq_fill_quads,                                            \
                                 mq_fill_scan_counts,mq_fill_scan_prefix,                               \
                                 mq_rast_cmds)                                                          \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_FILL_CMDS,fill_cmds) {                                       \
    mq_fill_cmds        SPN_MEMBER_VARRAY_UVEC4(fill_cmds);                                             \
  };                                                                                                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_FILL_QUADS,fill_quads) {                                     \
    mq_fill_quads       SPN_MEMBER_VARRAY_VEC4(fill_quads);                                             \
  };                                                                                                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_FILL_SCAN,fill_scan) {                                       \
    mq_fill_scan_counts SPN_MEMBER_FARRAY_UINT(fill_scan_counts,SPN_BLOCK_ID_TAG_PATH_COUNT * 4);       \
    SPN_VK_GLSL_ALIGN()                                                                                 \
    mq_fill_scan_prefix SPN_MEMBER_VARRAY_UVEC4(fill_scan_prefix);                                      \
  };                                                                                                    \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE,idx,                                                 \
                            SPN_VK_BINDING_RAST_CMDS,rast_cmds) {                                       \
    mq_rast_cmds        SPN_MEMBER_VARRAY_UVEC4(rast_cmds);                                             \
  };

//
// DESCRIPTOR: RASTERIZE POST
//

#define SPN_VK_BINDING_TTRKS          0

#define SPN_VK_DS_RASTERIZE_POST()                                                                              \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_RASTERIZE_POST, SPN_VK_BINDING_TTRKS, SPN_VK_EXTENT_TDRW, ttrks)

#define SPN_VK_GLSL_DS_RASTERIZE_POST(idx,mq_ttrks_meta,mq_ttrks_count,mq_ttrks_keys)   \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_RASTERIZE_POST,idx,                            \
                            SPN_VK_BINDING_TTRKS,ttrks) {                               \
    mq_ttrks_meta    SPN_MEMBER_STRUCT(spn_rc_meta,ttrks_meta);                         \
    SPN_VK_GLSL_ALIGN()                                                                 \
    mq_ttrks_count   SPN_MEMBER_UINT(ttrks_count);                                      \
    SPN_VK_GLSL_ALIGN()                                                                 \
    mq_ttrks_keys    SPN_MEMBER_VARRAY_UVEC2(ttrks_keys);                               \
  };

//
// DESCRIPTOR: TTCKS
//

#define SPN_VK_BINDING_TTCKS          0

#define SPN_VK_DS_TTCKS()                                                                               \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_TTCKS, SPN_VK_BINDING_TTCKS, SPN_VK_EXTENT_PDRW, ttcks)

#define SPN_VK_GLSL_DS_TTCKS(idx,mq_keys,mq_offsets)                            \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_TTCKS,idx,                             \
                            SPN_VK_BINDING_TTCKS,ttcks) {                       \
    mq_keys    SPN_MEMBER_FARRAY_UINT(ttcks_count,  4);                         \
    mq_offsets SPN_MEMBER_FARRAY_UINT(offsets_count,4);                         \
    SPN_VK_GLSL_ALIGN()                                                         \
    mq_offsets SPN_MEMBER_FARRAY_UINT(offsets,1<<SPN_TTCK_HI_BITS_YX);          \
    mq_keys    SPN_MEMBER_VARRAY_UVEC2(ttcks);                                  \
  };


//
// DESCRIPTOR: PLACE COMMANDS
//
// Implemented as a ring buffer.
//

#define SPN_VK_BINDING_PLACE          0

#define SPN_VK_DS_PLACE()                                                                                       \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_PLACE, SPN_VK_BINDING_PLACE, SPN_VK_EXTENT_PHW1G_TDRNS, place)

#define SPN_VK_GLSL_DS_PLACE(idx,mq_cmds)                       \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_PLACE,idx,             \
                            SPN_VK_BINDING_PLACE,cmds) {        \
    mq_cmds SPN_MEMBER_VARRAY_STRUCT(spn_cmd_place,cmds);       \
  };

//
// DESCRIPTOR: STYLING
//

#define SPN_VK_BINDING_STYLING        0

#define SPN_VK_DS_STYLING()                                                                                             \
    SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_STYLING, SPN_VK_BINDING_STYLING, SPN_VK_EXTENT_PHWN_PDRN, styling)

#define SPN_VK_GLSL_DS_STYLING(idx,mq_styling)                  \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_STYLING,idx,           \
                            SPN_VK_BINDING_STYLING,styling)  {  \
    mq_styling SPN_MEMBER_VARRAY_UINT(styling);                 \
  };

//
// DESCRIPTOR: SURFACE
//

#define SPN_VK_BINDING_SURFACE        0 // STORAGE_IMAGE

#ifdef SPN_KERNEL_RENDER_SURFACE_IS_IMAGE

#define SPN_VK_DS_SURFACE()                                                                                     \
    SPN_VK_DESC_TYPE_STORAGE_IMAGE(SPN_VK_DS_ID_SURFACE, SPN_VK_BINDING_SURFACE, SPN_VK_EXTENT_IMAGE, surface)

#define SPN_VK_GLSL_DS_SURFACE(idx,mq_surface,surface_type)     \
  SPN_VK_GLSL_LAYOUT_IMAGE2D(SPN_VK_DS_ID_SURFACE,idx,          \
                             SPN_VK_BINDING_SURFACE,            \
                             surface_type,surface);
#else

#define SPN_VK_DS_SURFACE()                                                                                     \
  SPN_VK_DESC_TYPE_STORAGE_BUFFER(SPN_VK_DS_ID_SURFACE, SPN_VK_BINDING_SURFACE, SPN_VK_EXTENT_PDRW, surface)

#define SPN_VK_GLSL_DS_SURFACE(idx,mq_surface,surface_type)     \
  SPN_VK_GLSL_LAYOUT_BUFFER(SPN_VK_DS_ID_SURFACE,idx,           \
                            SPN_VK_BINDING_SURFACE,             \
                            surface) {                          \
    mq_surface SPN_MEMBER_VARRAY_UNKNOWN(surface_type,surface); \
  };

#endif

/////////////////////////////////////////////////////////////////
//
// GLSL COMPUTE SHADER BINDINGS
//

//
// KERNEL: BLOCK POOL INIT
//

#define SPN_VK_GLSL_PUSH_KERNEL_BLOCK_POOL_INIT()       \
    SPN_VK_PUSH_UINT(bp_size)

#define SPN_VK_GLSL_DECL_KERNEL_BLOCK_POOL_INIT()                               \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,SPN_VK_GLSL_MQ_RW,writeonly,writeonly,writeonly); \
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
  SPN_VK_PUSH_UINT(pc_span)

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
  SPN_VK_GLSL_PUSH_KERNEL_PATHS_ALLOC()         \
  SPN_VK_PUSH_UINT(pc_head)                     \
  SPN_VK_PUSH_UINT(pc_rolling)                  \
  SPN_VK_PUSH_UINT(pc_size)

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
  SPN_VK_PUSH_UINT(cmd_count)

#define SPN_VK_GLSL_DECL_KERNEL_FILLS_SCAN()                              \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readonly,readonly,readonly,readonly);       \
  SPN_VK_GLSL_DS_RASTERIZE(2,                                             \
                                  readwrite,noaccess,                            \
                                  noaccess,noaccess,                             \
                                  noaccess);                                     \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN());

#define SPN_VK_HOST_DECL_KERNEL_FILLS_SCAN()                                        \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_SCAN,0,SPN_VK_DS_ID_BLOCK_POOL)                  \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_SCAN,1,SPN_VK_DS_ID_RASTERIZE_POST)              \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_SCAN,2,SPN_VK_DS_ID_RASTERIZE)                   \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_FILLS_SCAN,SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN())

//
// KERNEL: FILLS EXPAND
//

#define SPN_VK_GLSL_PUSH_KERNEL_FILLS_EXPAND()      \
  SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN()

#define SPN_VK_GLSL_DECL_KERNEL_FILLS_EXPAND()                      \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readonly,readonly,readonly,readonly); \
  SPN_VK_GLSL_DS_RASTERIZE(2,                                       \
                                  readonly,noaccess,                       \
                                  readonly,readonly,                       \
                                  writeonly);                              \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_FILLS_EXPAND());

#define SPN_VK_HOST_DECL_KERNEL_FILLS_EXPAND()                                      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_EXPAND,0,SPN_VK_DS_ID_BLOCK_POOL)                \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_EXPAND,1,SPN_VK_DS_ID_RASTERIZE_POST)            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_EXPAND,2,SPN_VK_DS_ID_RASTERIZE)                 \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_FILLS_EXPAND,SPN_VK_GLSL_PUSH_KERNEL_FILLS_EXPAND())

//
// KERNEL: FILLS DISPATCH
//

//
// Note: sets 0 and 1 are ignored
//

#define SPN_VK_GLSL_PUSH_KERNEL_FILLS_DISPATCH()

#define SPN_VK_GLSL_DECL_KERNEL_FILLS_DISPATCH()    \
  SPN_VK_GLSL_DS_RASTERIZE(2,                       \
                                  noaccess,noaccess,       \
                                  readwrite,noaccess,      \
                                  noaccess);

#define SPN_VK_HOST_DECL_KERNEL_FILLS_DISPATCH()                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_DISPATCH,0,SPN_VK_DS_ID_BLOCK_POOL)      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_DISPATCH,1,SPN_VK_DS_ID_RASTERIZE_POST)  \
  SPN_VK_HOST_DS(SPN_VK_P_ID_FILLS_DISPATCH,2,SPN_VK_DS_ID_RASTERIZE)


//
// KERNEL: RASTERIZE_XXX
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()    \
  SPN_VK_GLSL_PUSH_KERNEL_FILLS_SCAN()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()                             \
  SPN_VK_GLSL_DS_BLOCK_POOL    (0,readwrite,readwrite,readwrite,noaccess);  \
  SPN_VK_GLSL_DS_RASTERIZE_POST(1,noaccess,readwrite,writeonly);            \
  SPN_VK_GLSL_DS_RASTERIZE     (2,                                          \
                                       noaccess,readonly,                          \
                                       noaccess,noaccess,                          \
                                       readonly);                                  \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX());

//
// KERNEL: RASTERIZE_LINE
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_LINE()    \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_LINE()    \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_LINE()                                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_LINE,0,SPN_VK_DS_ID_BLOCK_POOL)                      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_LINE,1,SPN_VK_DS_ID_RASTERIZE_POST)                  \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_LINE,2,SPN_VK_DS_ID_RASTERIZE)                       \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_LINE,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_LINE())

//
// KERNEL: RASTERIZE_CUBIC
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_CUBIC()   \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_CUBIC()   \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_CUBIC()                                         \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_CUBIC,0,SPN_VK_DS_ID_BLOCK_POOL)                   \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_CUBIC,1,SPN_VK_DS_ID_RASTERIZE_POST)               \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_CUBIC,2,SPN_VK_DS_ID_RASTERIZE)                    \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_CUBIC,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_CUBIC())

//
// KERNEL: RASTERIZE_QUAD
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_QUAD()    \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_XXX()

#define SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_QUAD()    \
  SPN_VK_GLSL_DECL_KERNEL_RASTERIZE_XXX()

#define SPN_VK_HOST_DECL_KERNEL_RASTERIZE_QUAD()                                          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_QUAD,0,SPN_VK_DS_ID_BLOCK_POOL)                    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_QUAD,1,SPN_VK_DS_ID_RASTERIZE_POST)                \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_QUAD,2,SPN_VK_DS_ID_RASTERIZE)                     \
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
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_CUBIC,1,SPN_VK_DS_ID_RASTERIZE_POST)                   \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_CUBIC,2,SPN_VK_DS_ID_RASTERIZE)                        \
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
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_QUAD,1,SPN_VK_DS_ID_RASTERIZE_POST)                    \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERIZE_RAT_QUAD,2,SPN_VK_DS_ID_RASTERIZE)                         \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERIZE_RAT_QUAD,SPN_VK_GLSL_PUSH_KERNEL_RASTERIZE_RAT_QUAD())

//
// KERNELS: SORT AFTER RASTERIZE
//

//
// KERNEL: SEGMENT TTRK
//
// This kernel defines its own layout-compatible but arch-specific
// descriptor in order to harmonize with the HotSort library.
//

#define SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTRK()   \
  SPN_VK_PUSH_UINT(kv_offset_in)                 \
  SPN_VK_PUSH_UINT(kv_offset_out)                \
  SPN_VK_PUSH_UINT(kv_count)


#define SPN_VK_GLSL_DECL_KERNEL_SEGMENT_TTRK()                                              \
  SPN_VK_GLSL_DS_RASTERIZE_POST(1, readwrite, noaccess, readwrite);                         \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTRK());

#define SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTRK()                                             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTRK, 0, SPN_VK_DS_ID_BLOCK_POOL)                     \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTRK, 1, SPN_VK_DS_ID_RASTERIZE_POST)                 \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTRK, 2, SPN_VK_DS_ID_RASTERIZE)                      \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_SEGMENT_TTRK,SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTRK())

//
// KERNEL: RASTERS ALLOC
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC()     \
  SPN_VK_PUSH_UINT(bp_mask)                         \
  SPN_VK_PUSH_UINT(cmd_count)

// skips 1
#define SPN_VK_GLSL_DECL_KERNEL_RASTERS_ALLOC()                             \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,readonly,noaccess,writeonly);       \
  SPN_VK_GLSL_DS_RASTERIZE_POST(1,readwrite,noaccess,noaccess);             \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC());

#define SPN_VK_HOST_DECL_KERNEL_RASTERS_ALLOC()                                             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_ALLOC,0,SPN_VK_DS_ID_BLOCK_POOL)                       \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_ALLOC,1,SPN_VK_DS_ID_RASTERIZE_POST)                   \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERS_ALLOC,SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC())

//
// KERNEL: RASTERS PREFIX
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERS_PREFIX()    \
  SPN_VK_GLSL_PUSH_KERNEL_RASTERS_ALLOC()

// skips 1
#define SPN_VK_GLSL_DECL_KERNEL_RASTERS_PREFIX()                            \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readonly,readonly,readwrite,noaccess);        \
  SPN_VK_GLSL_DS_RASTERIZE_POST(1,readonly,readonly,readonly);              \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERS_PREFIX());

#define SPN_VK_HOST_DECL_KERNEL_RASTERS_PREFIX()                                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_PREFIX,0,SPN_VK_DS_ID_BLOCK_POOL)                      \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_PREFIX,1,SPN_VK_DS_ID_RASTERIZE_POST)                  \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERS_PREFIX,SPN_VK_GLSL_PUSH_KERNEL_RASTERS_PREFIX())

//
// KERNEL: PLACE
//

#define SPN_VK_GLSL_PUSH_KERNEL_PLACE()  \
  SPN_VK_PUSH_IVEC4(place_clip)

#define SPN_VK_GLSL_DECL_KERNEL_PLACE()                             \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,readonly,readonly); \
  SPN_VK_GLSL_DS_TTCKS     (1,readwrite,noaccess);                  \
  SPN_VK_GLSL_DS_PLACE     (2,readonly);                            \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_PLACE());

#define SPN_VK_HOST_DECL_KERNEL_PLACE()                              \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE,0,SPN_VK_DS_ID_BLOCK_POOL)        \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE,1,SPN_VK_DS_ID_TTCKS)             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PLACE,2,SPN_VK_DS_ID_PLACE)             \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_PLACE,SPN_VK_GLSL_PUSH_KERNEL_PLACE())

//
// KERNEL: SEGMENT TTCK
//
// This kernel defines its own layout-compatible but arch-specific
// descriptor in order to harmonize with the HotSort library.
//

#define SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTCK()   \
  SPN_VK_PUSH_UINT(kv_offset_in)                 \
  SPN_VK_PUSH_UINT(kv_offset_out)                \
  SPN_VK_PUSH_UINT(kv_count)


#define SPN_VK_GLSL_DECL_KERNEL_SEGMENT_TTCK()                           \
  SPN_VK_GLSL_DS_TTCKS(1,readwrite,readwrite);                           \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTCK());

#define SPN_VK_HOST_DECL_KERNEL_SEGMENT_TTCK()                                             \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTCK,0,SPN_VK_DS_ID_BLOCK_POOL)                       \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTCK,1,SPN_VK_DS_ID_TTCKS)                            \
  SPN_VK_HOST_DS(SPN_VK_P_ID_SEGMENT_TTCK,2,SPN_VK_DS_ID_PLACE)                            \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_SEGMENT_TTCK,SPN_VK_GLSL_PUSH_KERNEL_SEGMENT_TTCK())

//
// KERNEL: RENDER
//

#ifdef SPN_KERNEL_RENDER_SURFACE_IS_IMAGE

#define SPN_VK_GLSL_PUSH_KERNEL_RENDER()        \
  SPN_VK_PUSH_UINT_FARRAY(tile_clip,4)

#define SPN_VK_GLSL_DECL_KERNEL_RENDER()                                  \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,readonly,readonly);       \
  SPN_VK_GLSL_DS_TTCKS     (1,readonly,readonly);                         \
  SPN_VK_GLSL_DS_STYLING   (2,readonly);                                  \
  SPN_VK_GLSL_DS_SURFACE   (3,writeonly,SPN_KERNEL_RENDER_SURFACE_TYPE);  \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RENDER());

#else

#define SPN_VK_GLSL_PUSH_KERNEL_RENDER()         \
  SPN_VK_PUSH_UINT_FARRAY(tile_clip,4)           \
  SPN_VK_PUSH_UINT       (surface_pitch)

#define SPN_VK_GLSL_DECL_KERNEL_RENDER()                                    \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,noaccess,noaccess,readonly,readonly);         \
  SPN_VK_GLSL_DS_TTCKS     (1,readonly,readonly);                           \
  SPN_VK_GLSL_DS_STYLING   (2,readonly);                                    \
  SPN_VK_GLSL_DS_SURFACE   (3,writeonly,SPN_KERNEL_RENDER_SURFACE_TYPE);    \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RENDER());
#endif

#define SPN_VK_HOST_DECL_KERNEL_RENDER()                           \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,0,SPN_VK_DS_ID_BLOCK_POOL)     \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,1,SPN_VK_DS_ID_TTCKS)          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,2,SPN_VK_DS_ID_STYLING)        \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RENDER,3,SPN_VK_DS_ID_SURFACE)        \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RENDER,SPN_VK_GLSL_PUSH_KERNEL_RENDER())

//
// KERNEL: PATHS RECLAIM
//

#define SPN_VK_GLSL_PUSH_KERNEL_PATHS_RECLAIM()                                 \
  SPN_VK_PUSH_UINT(bp_mask)                                                     \
  SPN_VK_PUSH_UINT_VARRAY(path_ids,SPN_KERNEL_PATHS_RECLAIM_MAX_RECLAIM_IDS)

#define SPN_VK_GLSL_DECL_KERNEL_PATHS_RECLAIM()                           \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,readwrite,readonly,readonly);     \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_PATHS_RECLAIM());

#define SPN_VK_HOST_DECL_KERNEL_PATHS_RECLAIM()                          \
  SPN_VK_HOST_DS(SPN_VK_P_ID_PATHS_RECLAIM,0,SPN_VK_DS_ID_BLOCK_POOL)    \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_PATHS_RECLAIM,SPN_VK_GLSL_PUSH_KERNEL_PATHS_RECLAIM())

//
// KERNEL: RASTERS RECLAIM
//

#define SPN_VK_GLSL_PUSH_KERNEL_RASTERS_RECLAIM()                                       \
  SPN_VK_PUSH_UINT(bp_mask)                                                             \
  SPN_VK_PUSH_UINT_VARRAY(raster_ids,SPN_KERNEL_RASTERS_RECLAIM_MAX_RECLAIM_IDS)

#define SPN_VK_GLSL_DECL_KERNEL_RASTERS_RECLAIM()                     \
  SPN_VK_GLSL_DS_BLOCK_POOL(0,readwrite,readwrite,readonly,readonly); \
  SPN_VK_GLSL_PUSH(SPN_VK_GLSL_PUSH_KERNEL_RASTERS_RECLAIM());

#define SPN_VK_HOST_DECL_KERNEL_RASTERS_RECLAIM()                                               \
  SPN_VK_HOST_DS(SPN_VK_P_ID_RASTERS_RECLAIM,0,SPN_VK_DS_ID_BLOCK_POOL)                         \
  SPN_VK_HOST_PUSH(SPN_VK_P_ID_RASTERS_RECLAIM,SPN_VK_GLSL_PUSH_KERNEL_RASTERS_RECLAIM())

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_SPN_VK_LAYOUTS_H_
