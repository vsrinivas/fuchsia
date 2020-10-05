// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CORE_C_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CORE_C_H_

/////////////////////////////////////////////////////////////////
//
// C99
//

#include <stdint.h>

//
// clang-format off
//

struct spn_vec2   { float    x; float    y; };
struct spn_vec4   { float    x; float    y; float    z; float    w; };
struct spn_uvec2  { uint32_t x; uint32_t y; };
struct spn_uvec4  { uint32_t x; uint32_t y; uint32_t z; uint32_t w; };
struct spn_ivec4  { int32_t  x; int32_t  y; int32_t  z; int32_t  w; };
struct spn_mat2x2 { float    a; float    b; float    c; float    d; }; // GLSL defaults to column major

#define SPN_TYPE_UINT    uint32_t
#define SPN_TYPE_INT     int32_t
#define SPN_TYPE_VEC2    struct spn_vec2
#define SPN_TYPE_VEC4    struct spn_vec4
#define SPN_TYPE_UVEC2   struct spn_uvec2
#define SPN_TYPE_UVEC4   struct spn_uvec4
#define SPN_TYPE_IVEC4   struct spn_ivec4
#define SPN_TYPE_MAT2X2  struct spn_mat2x2

//
//
//

#include "core.h"
#include "common/macros.h"

//
//
//

#define SPN_UINT_MAX              UINT32_MAX

//
//
//

#define SPN_BITS_TO_MASK(n)       BITS_TO_MASK_MACRO(n)
#define SPN_BITS_TO_MASK_AT(n,b)  BITS_TO_MASK_AT_MACRO(n,b)


//
// TAGGED BLOCK ID
//

typedef uint32_t spn_tagged_block_id_t;

union spn_tagged_block_id
{
  uint32_t   u32;

  struct {
    uint32_t tag : SPN_TAGGED_BLOCK_ID_BITS_TAG;
    uint32_t id  : SPN_TAGGED_BLOCK_ID_BITS_ID;
  };
};

//
// BLOCK ID
//

typedef uint32_t spn_block_id_t;


//
// PATH
//

union spn_path_prims
{
  uint32_t   array[SPN_BLOCK_ID_TAG_PATH_COUNT];

  struct {
    uint32_t lines;
    uint32_t quads;
    uint32_t cubics;
    uint32_t rat_quads;
    uint32_t rat_cubics;
  } named;
};

union spn_path_header
{
  uint32_t               array[SPN_PATH_HEAD_DWORDS];

  struct {
    uint32_t             handle; // host handle
    uint32_t             blocks; // total number of blocks in entire path object -- includes nodes and segments
    uint32_t             nodes;  // number of trailing path node blocks -- not including head

    union spn_path_prims prims;

    struct spn_vec4      bounds;
  } named;
};

//
// STYLING GROUP NODE
//

struct spn_group_node
 {
   struct spn_group_parents parents; // path of parent groups leading back to root
   struct spn_group_range   range;   // range of layers enclosed by this group
   struct spn_group_cmds    cmds;    // enter/leave command indices
 };

//
//
//

#if 0

union spn_gradient_vector
{
  skc_float4               f32v4;

  struct {
    skc_float              dx;
    skc_float              p0;
    skc_float              dy;
    skc_float              denom;
  };

  union skc_gradient_slope slopes[4];
};

#endif

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_CORE_C_H_
