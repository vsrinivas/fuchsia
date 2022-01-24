// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_PUSH_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_PUSH_H_

//
// All shared types and structures.
//
// We assume C-like structure layout on the host and device.
//
// The current descriptors are simple enough that std430 is sufficient but the
// "scalar block layout" may be required in the future.
//

////////////////////////////////////////////////////////////////////
//
// GLSL
//
#ifdef VULKAN

//
// Common includes
//
#include "bufref.h"
#include "core_glsl.h"
#include "expand_x.h"
#include "macros.h"

//
//
//
// clang-format off
#define SPN_MEMBER_ALIGN(member_)                     layout(align = SPN_MEMBER_ALIGN_LIMIT) member_

#define SPN_MEMBER(qual_, type_, name_)               qual_ type_ name_
#define SPN_MEMBER_FARRAY(qual_, type_, len_, name_)  qual_ type_ name_[len_]
#define SPN_MEMBER_VARRAY(qual_, type_, name_)        qual_ type_ name_[]
#define SPN_MEMBER_STRUCT(qual_, type_, name_)        qual_ type_ name_
#define SPN_MEMBER_VARRAY_STRUCT(qual_, type_, name_) qual_ type_ name_[]

#define SPN_STRUCT_TYPE(name_)    spinel_##name_
#define SPN_STRUCT_DEFINE(name_)  struct SPN_STRUCT_TYPE(name_)

#define SPN_BUFFER_TYPE(name_)    spinel_buffer_##name_
#define SPN_BUFFER_DEFINE(name_)  layout(buffer_reference, std430) buffer SPN_BUFFER_TYPE(name_)

#define SPN_PUSH_TYPE(name_)      spinel_push_##name_
#define SPN_PUSH_DEFINE(name_)    struct SPN_PUSH_TYPE(name_)
// clang-format on

#define SPN_PUSH_LAYOUT(name_)                                                                     \
  layout(push_constant) uniform block_push                                                         \
  {                                                                                                \
    SPN_PUSH_TYPE(name_) push;                                                                     \
  }

//
// No-op qualifiers
//
// clang-format off
#define readwrite
#define noaccess          readonly  // This is `readonly` until `noaccess` is a valid qualifier
#define struct_member
#define push_member
// clang-format on

//
// Types
//
// clang-format off
#define SPN_TYPE_U32      uint32_t
#define SPN_TYPE_I32      int32_t
#define SPN_TYPE_U32VEC2  u32vec2
#define SPN_TYPE_U32VEC4  u32vec4
#define SPN_TYPE_I32VEC4  i32vec4
#define SPN_TYPE_F32VEC2  f32vec2
#define SPN_TYPE_F32VEC4  f32vec4
#define SPN_TYPE_MAT2X2   mat2x2
// clang-format on

//
//
//

////////////////////////////////////////////////////////////////////
//
// C/C++
//
#else

//
// Common includes
//
#include <stddef.h>

#include "bufref.h"
#include "common/macros.h"
#include "core_c.h"

//
//
// clang-format off
#define SPN_MEMBER_ALIGN(member_)                     ALIGN_MACRO(SPN_MEMBER_ALIGN_LIMIT) member_

#define SPN_MEMBER(qual_, type_, name_)               qual_ type_ name_
#define SPN_MEMBER_FARRAY(qual_, type_, len_, name_)  qual_ type_ name_[len_]
#define SPN_MEMBER_VARRAY(qual_, type_, name_)        qual_ type_ name_[]
#define SPN_MEMBER_STRUCT(qual_, type_, name_)        qual_ type_ name_
#define SPN_MEMBER_VARRAY_STRUCT(qual_, type_, name_) qual_ type_ name_[]

#define SPN_STRUCT_TYPE(name_)                        struct spinel_##name_
#define SPN_STRUCT_DEFINE(name_)                      SPN_STRUCT_TYPE(name_)

#define SPN_BUFFER_TYPE(name_)                        struct spinel_buffer_##name_
#define SPN_BUFFER_DEFINE(name_)                      SPN_BUFFER_TYPE(name_)
#define SPN_BUFFER_OFFSETOF(name_, member_)           offsetof(SPN_BUFFER_TYPE(name_), member_)
#define SPN_BUFFER_MEMBER_SIZE(name_, member_)        MEMBER_SIZE_MACRO(SPN_BUFFER_TYPE(name_), member_)

#define SPN_PUSH_TYPE(name_)                          struct spinel_push_##name_
#define SPN_PUSH_DEFINE(name_)                        SPN_PUSH_TYPE(name_)
#define SPN_PUSH_OFFSETOF(name_, member_)             offsetof(SPN_PUSH_TYPE(name_), member_)
#define SPN_PUSH_MEMBER_SIZE(name_, member_)          MEMBER_SIZE_MACRO(SPN_PUSH_TYPE(name_), member_)
// clang-format on

//
// No-op qualifiers
//
#define readonly
#define writeonly
#define readwrite
#define noaccess
#define struct_member
#define push_member

//
// Types
//
struct spinel_u32vec2
{
  uint32_t x;
  uint32_t y;
};

struct spinel_u32vec4
{
  uint32_t x;
  uint32_t y;
  uint32_t z;
  uint32_t w;
};

struct spinel_i32vec4
{
  int32_t x;
  int32_t y;
  int32_t z;
  int32_t w;
};

struct spinel_f32vec2
{
  float x;
  float y;
};

struct spinel_f32vec4
{
  float x;
  float y;
  float z;
  float w;
};

struct spinel_mat2x2
{
  float a;
  float b;
  float c;
  float d;
};  // GLSL defaults to column major

// clang-format off
#define SPN_TYPE_U32      uint32_t
#define SPN_TYPE_I32      int32_t
#define SPN_TYPE_U32VEC2  struct spinel_u32vec2
#define SPN_TYPE_U32VEC4  struct spinel_u32vec4
#define SPN_TYPE_I32VEC4  struct spinel_i32vec4
#define SPN_TYPE_F32VEC2  struct spinel_f32vec2
#define SPN_TYPE_F32VEC4  struct spinel_f32vec4
#define SPN_TYPE_MAT2X2   struct spinel_mat2x2
// clang-format on

//
//
//

#endif

////////////////////////////////////////////////////////////////////
//
// STRUCTURES
//

#define SPN_STRUCT_DEFINE_CMD_PLACE()                                                              \
  SPN_STRUCT_DEFINE(cmd_place)                                                                     \
  {                                                                                                \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, raster_h);                                             \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, layer_id);                                             \
    SPN_MEMBER_FARRAY(struct_member, SPN_TYPE_U32, 2, txty);                                       \
  }

#define SPN_STRUCT_DEFINE_RC_META()                                                                \
  SPN_STRUCT_DEFINE(rc_meta)                                                                       \
  {                                                                                                \
    SPN_MEMBER_FARRAY(struct_member, SPN_TYPE_U32VEC2, SPN_RASTER_COHORT_METAS_SIZE, alloc);       \
    SPN_MEMBER_FARRAY(struct_member, SPN_TYPE_U32, SPN_RASTER_COHORT_METAS_SIZE, rk_off);          \
    SPN_MEMBER_FARRAY(struct_member, SPN_TYPE_U32, SPN_RASTER_COHORT_METAS_SIZE, blocks);          \
    SPN_MEMBER_FARRAY(struct_member, SPN_TYPE_U32, SPN_RASTER_COHORT_METAS_SIZE, ttpks);           \
    SPN_MEMBER_FARRAY(struct_member, SPN_TYPE_U32, SPN_RASTER_COHORT_METAS_SIZE, ttrks);           \
  }

#define SPN_STRUCT_DEFINE_LAYER_NODE()                                                             \
  SPN_STRUCT_DEFINE(layer_node)                                                                    \
  {                                                                                                \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, cmds);                                                 \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, parent);                                               \
  }

#define SPN_STRUCT_DEFINE_GROUP_PARENTS()                                                          \
  SPN_STRUCT_DEFINE(group_parents)                                                                 \
  {                                                                                                \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, depth);                                                \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, base);                                                 \
  }

#define SPN_STRUCT_DEFINE_GROUP_RANGE()                                                            \
  SPN_STRUCT_DEFINE(group_range)                                                                   \
  {                                                                                                \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, lo);                                                   \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, hi);                                                   \
  }

#define SPN_STRUCT_DEFINE_GROUP_CMDS()                                                             \
  SPN_STRUCT_DEFINE(group_cmds)                                                                    \
  {                                                                                                \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, enter);                                                \
    SPN_MEMBER(struct_member, SPN_TYPE_U32, leave);                                                \
  }

#define SPN_STRUCT_DEFINE_GROUP_NODE()                                                             \
  SPN_STRUCT_DEFINE(group_node)                                                                    \
  {                                                                                                \
    SPN_MEMBER_STRUCT(struct_member, SPN_STRUCT_TYPE(group_parents), parents);                     \
    SPN_MEMBER_STRUCT(struct_member, SPN_STRUCT_TYPE(group_range), range);                         \
    SPN_MEMBER_STRUCT(struct_member, SPN_STRUCT_TYPE(group_cmds), cmds);                           \
  }

////////////////////////////////////////////////////////////////////
//
// BUFFER LAYOUTS
//
// Define device-side buffers and matching host-side structs.
//

//
// Align C99 and GLSL buffer members so the variable-sized arrays are
// conservatively aligned to a device's memory transaction boundary.
//
#define SPN_MEMBER_ALIGN_LIMIT 256  // (64 * 4)

//
// BLOCK POOL
//
#define SPN_BUFFER_DEFINE_BLOCK_POOL_IDS(qual_atomics_, qual_ids_)                                 \
  SPN_BUFFER_DEFINE(block_pool_ids)                                                                \
  {                                                                                                \
    SPN_MEMBER_FARRAY(qual_atomics_, SPN_TYPE_U32, 2, atomics);                                    \
    SPN_MEMBER_ALIGN(SPN_MEMBER_VARRAY(qual_ids_, SPN_TYPE_U32, ids));                             \
  }

#define SPN_BUFFER_DEFINE_BLOCK_POOL_BLOCKS(qual_blocks_)                                          \
  SPN_BUFFER_DEFINE(block_pool_blocks)                                                             \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_blocks_, SPN_TYPE_U32, extent);                                         \
  }

#define SPN_BUFFER_DEFINE_BLOCK_POOL_BLOCKS_U32VEC4(qual_blocks_)                                  \
  SPN_BUFFER_DEFINE(block_pool_blocks_u32vec4)                                                     \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_blocks_, SPN_TYPE_U32VEC4, extent);                                     \
  }

#define SPN_BUFFER_DEFINE_BLOCK_POOL_HOST_MAP(qual_host_map_)                                      \
  SPN_BUFFER_DEFINE(block_pool_host_map)                                                           \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_host_map_, SPN_TYPE_U32, extent);                                       \
  }

//
// PATHS COPY
//
#define SPN_BUFFER_DEFINE_PATHS_COPY_ALLOC(qual_alloc_)                                            \
  SPN_BUFFER_DEFINE(paths_copy_alloc)                                                              \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_alloc_, SPN_TYPE_U32, extent);                                          \
  }

#define SPN_BUFFER_DEFINE_PATHS_COPY_RING(qual_ring_)                                              \
  SPN_BUFFER_DEFINE(paths_copy_ring)                                                               \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_ring_, SPN_TYPE_U32, extent);                                           \
  }

//
// RASTERIZE
//
#define SPN_BUFFER_DEFINE_RASTERIZE_FILL_CMDS(qual_cmds_)                                          \
  SPN_BUFFER_DEFINE(rasterize_fill_cmds)                                                           \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_cmds_, SPN_TYPE_U32VEC4, extent);                                       \
  }

#define SPN_BUFFER_DEFINE_RASTERIZE_FILL_QUADS(qual_quads_)                                        \
  SPN_BUFFER_DEFINE(rasterize_fill_quads)                                                          \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_quads_, SPN_TYPE_F32VEC4, extent);                                      \
  }

#define SPN_BUFFER_DEFINE_RASTERIZE_FILL_SCAN(qual_dispatch_, qual_counts_, qual_prefix_)          \
  SPN_BUFFER_DEFINE(rasterize_fill_scan)                                                           \
  {                                                                                                \
    SPN_MEMBER_FARRAY(qual_dispatch_, SPN_TYPE_U32VEC4, SPN_RAST_TYPE_COUNT, dispatch);            \
    SPN_MEMBER_FARRAY(qual_counts_, SPN_TYPE_U32, SPN_RAST_TYPE_COUNT, counts);                    \
    SPN_MEMBER_ALIGN(SPN_MEMBER_VARRAY(qual_prefix_, SPN_TYPE_U32VEC4, prefix));                   \
  }

#define SPN_BUFFER_DEFINE_RASTERIZE_RAST_CMDS(qual_cmds_)                                          \
  SPN_BUFFER_DEFINE(rasterize_rast_cmds)                                                           \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_cmds_, SPN_TYPE_U32VEC4, extent);                                       \
  }

//
// TTRKS
//
#define SPN_BUFFER_DEFINE_TTRKS(qual_meta_, qual_count_, qual_keyvals_)                            \
  SPN_BUFFER_DEFINE(ttrks)                                                                         \
  {                                                                                                \
    SPN_MEMBER_STRUCT(qual_meta_, SPN_STRUCT_TYPE(rc_meta), meta);                                 \
    SPN_MEMBER(qual_count_, SPN_TYPE_U32VEC4, count_dispatch);                                     \
    SPN_MEMBER_ALIGN(SPN_MEMBER_VARRAY(qual_keyvals_, SPN_TYPE_U32VEC2, keyvals));                 \
  }

//
// TTRKS HEADER
//
#define SPN_BUFFER_DEFINE_TTRKS_HEADER(qual_meta_, qual_count_)                                    \
  SPN_BUFFER_DEFINE(ttrks_header)                                                                  \
  {                                                                                                \
    SPN_MEMBER_STRUCT(qual_meta_, SPN_STRUCT_TYPE(rc_meta), meta);                                 \
    SPN_MEMBER(qual_count_, SPN_TYPE_U32VEC4, count_dispatch);                                     \
  }

//
// TTRK KEYVALS
//
#define SPN_BUFFER_DEFINE_TTRK_KEYVALS(qual_keyvals_)                                              \
  SPN_BUFFER_DEFINE(ttrk_keyvals)                                                                  \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_keyvals_, SPN_TYPE_U32VEC2, extent);                                    \
  }

//
// RASTER IDS
//
#define SPN_BUFFER_DEFINE_RASTER_IDS(qual_raster_ids_)                                             \
  SPN_BUFFER_DEFINE(raster_ids)                                                                    \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_raster_ids_, SPN_TYPE_U32, extent);                                     \
  }

//
// TTCKS
//
#define SPN_BUFFER_DEFINE_TTCKS(qual_segment_, qual_render_, qual_offsets_, qual_keyvals_)         \
  SPN_BUFFER_DEFINE(ttcks)                                                                         \
  {                                                                                                \
    SPN_MEMBER(qual_segment_, SPN_TYPE_U32VEC4, segment_dispatch);                                 \
    SPN_MEMBER(qual_render_, SPN_TYPE_U32VEC4, render_dispatch);                                   \
    SPN_MEMBER_ALIGN(SPN_MEMBER_FARRAY(qual_offsets_, /**/                                         \
                                       SPN_TYPE_U32,                                               \
                                       (1 << SPN_TTCK_HI_BITS_XY),                                 \
                                       offsets));                                                  \
    SPN_MEMBER_VARRAY(qual_keyvals_, SPN_TYPE_U32VEC2, ttck_keyvals);                              \
  }

//
// TTCKS HEADER
//
#define SPN_BUFFER_DEFINE_TTCKS_HEADER(qual_segment_, qual_render_, qual_offsets_)                 \
  SPN_BUFFER_DEFINE(ttcks_header)                                                                  \
  {                                                                                                \
    SPN_MEMBER(qual_segment_, SPN_TYPE_U32VEC4, segment_dispatch);                                 \
    SPN_MEMBER(qual_render_, SPN_TYPE_U32VEC4, render_dispatch);                                   \
    SPN_MEMBER_ALIGN(SPN_MEMBER_FARRAY(qual_offsets_, /**/                                         \
                                       SPN_TYPE_U32,                                               \
                                       (1 << SPN_TTCK_HI_BITS_XY),                                 \
                                       offsets));                                                  \
  }

//
// TTCK KEYVALS
//
#define SPN_BUFFER_DEFINE_TTCK_KEYVALS(qual_keyvals_)                                              \
  SPN_BUFFER_DEFINE(ttck_keyvals)                                                                  \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_keyvals_, SPN_TYPE_U32VEC2, extent);                                    \
  }

//
// PLACE
//
#define SPN_BUFFER_DEFINE_PLACE(qual_cmds_)                                                        \
  SPN_BUFFER_DEFINE(place)                                                                         \
  {                                                                                                \
    SPN_MEMBER_VARRAY_STRUCT(qual_cmds_, SPN_STRUCT_TYPE(cmd_place), extent);                      \
  }

//
// STYLING
//
#define SPN_BUFFER_DEFINE_STYLING(qual_styling_)                                                   \
  SPN_BUFFER_DEFINE(styling)                                                                       \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_styling_, SPN_TYPE_U32, extent);                                        \
  }

//
// SURFACE
//
#define SPN_BUFFER_DEFINE_SURFACE(qual_surface_)                                                   \
  SPN_BUFFER_DEFINE(surface)                                                                       \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_surface_, SPN_TYPE_U32, extent);                                        \
  }

//
// RECLAIM
//
#define SPN_BUFFER_DEFINE_RECLAIM(qual_ring_)                                                      \
  SPN_BUFFER_DEFINE(reclaim)                                                                       \
  {                                                                                                \
    SPN_MEMBER_VARRAY(qual_ring_, SPN_TYPE_U32, extent);                                           \
  }

////////////////////////////////////////////////////////////////////
//
// PUSH CONSTANTS
//
// Define the push constant structures shared by the host and device.
//
////////////////////////////////////////////////////////////////////

//
// BLOCK POOL INIT
//
#define SPN_PUSH_DEFINE_BLOCK_POOL_INIT()                                                          \
  SPN_PUSH_DEFINE(block_pool_init)                                                                 \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_TYPE_U32, bp_size);                                                \
  }

#define SPN_PUSH_LAYOUT_BLOCK_POOL_INIT() SPN_PUSH_LAYOUT(block_pool_init)

//
// FILL SCAN
//
#define SPN_PUSH_DEFINE_FILL_SCAN()                                                                \
  SPN_PUSH_DEFINE(fill_scan)                                                                       \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_fill_scan);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_fill_cmds);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_host_map);                             \
    SPN_MEMBER(push_member, SPN_TYPE_U32, cmd_head);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, cmd_size);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, cmd_span);                                               \
  }

#define SPN_PUSH_LAYOUT_FILL_SCAN() SPN_PUSH_LAYOUT(fill_scan)

//
// FILL DISPATCH
//
#define SPN_PUSH_DEFINE_FILL_DISPATCH()                                                            \
  SPN_PUSH_DEFINE(fill_dispatch)                                                                   \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_fill_scan);                             \
  }

#define SPN_PUSH_LAYOUT_FILL_DISPATCH() SPN_PUSH_LAYOUT(fill_dispatch)

//
// FILL EXPAND
//
#define SPN_PUSH_DEFINE_FILL_EXPAND()                                                              \
  SPN_PUSH_DEFINE(fill_expand)                                                                     \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_fill_scan);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_fill_cmds);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_host_map);                             \
    SPN_MEMBER(push_member, SPN_TYPE_U32, cmd_head);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, cmd_size);                                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_rast_cmds);                             \
  }

#define SPN_PUSH_LAYOUT_FILL_EXPAND() SPN_PUSH_LAYOUT(fill_expand)

//
// PATHS ALLOC
//
#define SPN_PUSH_DEFINE_PATHS_ALLOC()                                                              \
  SPN_PUSH_DEFINE(paths_alloc)                                                                     \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_paths_copy_alloc);                                \
    SPN_MEMBER(push_member, SPN_TYPE_U32, pc_alloc_idx);                                           \
    SPN_MEMBER(push_member, SPN_TYPE_U32, pc_span);                                                \
  }

#define SPN_PUSH_LAYOUT_PATHS_ALLOC() SPN_PUSH_LAYOUT(paths_alloc)

//
// PATHS COPY
//
#define SPN_PUSH_DEFINE_PATHS_COPY()                                                               \
  SPN_PUSH_DEFINE(paths_copy)                                                                      \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_host_map);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_paths_copy_alloc);                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_paths_copy_ring);                                 \
    SPN_MEMBER(push_member, SPN_TYPE_U32, bp_mask);                                                \
    SPN_MEMBER(push_member, SPN_TYPE_U32, pc_alloc_idx);                                           \
    SPN_MEMBER(push_member, SPN_TYPE_U32, pc_span);                                                \
    SPN_MEMBER(push_member, SPN_TYPE_U32, pc_head);                                                \
    SPN_MEMBER(push_member, SPN_TYPE_U32, pc_rolling);                                             \
    SPN_MEMBER(push_member, SPN_TYPE_U32, pc_size);                                                \
  }

#define SPN_PUSH_LAYOUT_PATHS_COPY() SPN_PUSH_LAYOUT(paths_copy)

//
// PATHS RECLAIM / RASTERS RECLAIM
//
#define SPN_PUSH_DEFINE_RECLAIM()                                                                  \
  SPN_PUSH_DEFINE(reclaim)                                                                         \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_reclaim);                                         \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_host_map);                             \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ring_size);                                              \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ring_head);                                              \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ring_span);                                              \
    SPN_MEMBER(push_member, SPN_TYPE_U32, bp_mask);                                                \
  }

#define SPN_PUSH_LAYOUT_RECLAIM() SPN_PUSH_LAYOUT(reclaim)

//
// PLACE TTPK / PLACE TTSK
//
#define SPN_PUSH_DEFINE_PLACE()                                                                    \
  SPN_PUSH_DEFINE(place)                                                                           \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_TYPE_I32VEC4, place_clip);                                         \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_host_map);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttcks);                                           \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_place);                                           \
    SPN_MEMBER(push_member, SPN_TYPE_U32, place_head);                                             \
    SPN_MEMBER(push_member, SPN_TYPE_U32, place_span);                                             \
    SPN_MEMBER(push_member, SPN_TYPE_U32, place_size);                                             \
  }

#define SPN_PUSH_LAYOUT_PLACE() SPN_PUSH_LAYOUT(place)

//
// RASTERS ALLOC
//
#define SPN_PUSH_DEFINE_RASTERS_ALLOC()                                                            \
  SPN_PUSH_DEFINE(rasters_alloc)                                                                   \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_raster_ids);                                      \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrks_header);                                    \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrk_keyvals);                                    \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_host_map);                             \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ids_size);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ids_head);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ids_span);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, bp_mask);                                                \
  }

#define SPN_PUSH_LAYOUT_RASTERS_ALLOC() SPN_PUSH_LAYOUT(rasters_alloc)

//
// RASTERS PREFIX
//
#define SPN_PUSH_DEFINE_RASTERS_PREFIX()                                                           \
  SPN_PUSH_DEFINE(rasters_prefix)                                                                  \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrks_header);                                    \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrk_keyvals);                                    \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ids_size);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ids_head);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, ids_span);                                               \
    SPN_MEMBER(push_member, SPN_TYPE_U32, bp_mask);                                                \
  }

#define SPN_PUSH_LAYOUT_RASTERS_PREFIX() SPN_PUSH_LAYOUT(rasters_prefix)

//
// RENDER
//
#define SPN_PUSH_DEFINE_RENDER()                                                                   \
  SPN_PUSH_DEFINE(render)                                                                          \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_TYPE_I32VEC4, tile_clip);                                          \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_surface);                                         \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_styling);                                         \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttcks_header);                                    \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttck_keyvals);                                    \
    SPN_MEMBER(push_member, SPN_TYPE_U32, row_pitch);                                              \
  }

#define SPN_PUSH_LAYOUT_RENDER() SPN_PUSH_LAYOUT(render)

//
// RENDER_DISPATCH
//
#define SPN_PUSH_DEFINE_RENDER_DISPATCH()                                                          \
  SPN_PUSH_DEFINE(render_dispatch)                                                                 \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttcks_header);                                    \
  }

#define SPN_PUSH_LAYOUT_RENDER_DISPATCH() SPN_PUSH_LAYOUT(render_dispatch)

//
// RASTERIZE LINE/QUAD/CUBIC/PROJ_LINE/PROJ_QUAD/PROJ_CUBIC/RAT_QUAD/RAT_CUBIC
//
#define SPN_PUSH_DEFINE_RASTERIZE()                                                                \
  SPN_PUSH_DEFINE(rasterize)                                                                       \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_ids);                                  \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_block_pool_blocks);                               \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_fill_quads);                            \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_fill_scan);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_rasterize_rast_cmds);                             \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrks);                                           \
    SPN_MEMBER(push_member, SPN_TYPE_U32, bp_mask);                                                \
  }

#define SPN_PUSH_LAYOUT_RASTERIZE() SPN_PUSH_LAYOUT(rasterize)

//
// TTCKS SEGMENT
//
#define SPN_PUSH_DEFINE_TTCKS_SEGMENT()                                                            \
  SPN_PUSH_DEFINE(ttcks_segment)                                                                   \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttcks_header);                                    \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttck_keyvals);                                    \
  }

#define SPN_PUSH_LAYOUT_TTCKS_SEGMENT() SPN_PUSH_LAYOUT(ttcks_segment)

//
// TTCKS SEGMENT DISPATCH
//
#define SPN_PUSH_DEFINE_TTCKS_SEGMENT_DISPATCH()                                                   \
  SPN_PUSH_DEFINE(ttcks_segment_dispatch)                                                          \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttcks_header);                                    \
  }

#define SPN_PUSH_LAYOUT_TTCKS_SEGMENT_DISPATCH() SPN_PUSH_LAYOUT(ttcks_segment_dispatch)

//
// TTRKS SEGMENT
//
#define SPN_PUSH_DEFINE_TTRKS_SEGMENT()                                                            \
  SPN_PUSH_DEFINE(ttrks_segment)                                                                   \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrks_header);                                    \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrk_keyvals);                                    \
  }

#define SPN_PUSH_LAYOUT_TTRKS_SEGMENT() SPN_PUSH_LAYOUT(ttrks_segment)

//
// TTRKS SEGMENT DISPATCH
//
#define SPN_PUSH_DEFINE_TTRKS_SEGMENT_DISPATCH()                                                   \
  SPN_PUSH_DEFINE(ttrks_segment_dispatch)                                                          \
  {                                                                                                \
    SPN_MEMBER(push_member, SPN_DEVADDR, devaddr_ttrks_header);                                    \
  }

#define SPN_PUSH_LAYOUT_TTRKS_SEGMENT_DISPATCH() SPN_PUSH_LAYOUT(ttrks_segment_dispatch)

//
// Define all structs on host and device
//
SPN_STRUCT_DEFINE_CMD_PLACE();
SPN_STRUCT_DEFINE_RC_META();
SPN_STRUCT_DEFINE_LAYER_NODE();
SPN_STRUCT_DEFINE_GROUP_PARENTS();
SPN_STRUCT_DEFINE_GROUP_RANGE();
SPN_STRUCT_DEFINE_GROUP_CMDS();

//
// Define all push structs on host and device
//
SPN_PUSH_DEFINE_BLOCK_POOL_INIT();
SPN_PUSH_DEFINE_FILL_DISPATCH();
SPN_PUSH_DEFINE_FILL_EXPAND();
SPN_PUSH_DEFINE_FILL_SCAN();
SPN_PUSH_DEFINE_PATHS_ALLOC();
SPN_PUSH_DEFINE_PATHS_COPY();
SPN_PUSH_DEFINE_RECLAIM();
SPN_PUSH_DEFINE_PLACE();
SPN_PUSH_DEFINE_RASTERS_ALLOC();
SPN_PUSH_DEFINE_RASTERS_PREFIX();
SPN_PUSH_DEFINE_RENDER();
SPN_PUSH_DEFINE_RENDER_DISPATCH();
SPN_PUSH_DEFINE_RASTERIZE();
SPN_PUSH_DEFINE_TTCKS_SEGMENT();
SPN_PUSH_DEFINE_TTCKS_SEGMENT_DISPATCH();
SPN_PUSH_DEFINE_TTRKS_SEGMENT();
SPN_PUSH_DEFINE_TTRKS_SEGMENT_DISPATCH();

//
// Define buffers on host
//
#ifndef VULKAN

SPN_BUFFER_DEFINE_BLOCK_POOL_IDS(readwrite, readwrite);
SPN_BUFFER_DEFINE_RASTERIZE_FILL_SCAN(readwrite, readwrite, readwrite);
SPN_BUFFER_DEFINE_TTCKS(readwrite, readwrite, readwrite, readwrite);
SPN_BUFFER_DEFINE_TTRKS(readwrite, readwrite, readwrite);

#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_PUSH_H_
