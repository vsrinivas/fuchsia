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
struct spn_mat2x2 { float    a; float    b; float    c; float    d; };

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

union spn_path_header
{
  uint32_t           u32aN[SPN_PATH_HEAD_DWORDS];

  struct {
    uint32_t         handle;   // host handle
    uint32_t         blocks;   // # of S-segment blocks in path
    uint32_t         nodes;    // # of S-segment node blocks -- not including header
    uint32_t         na;       // unused

    struct spn_uvec4 prims;

    struct spn_vec4  bounds;
  };
};

//
// TTCK
//

union spn_ttck
{
  SPN_TYPE_UVEC2 u32v2;

  struct {
    uint32_t     ttxb_id  : SPN_TAGGED_BLOCK_ID_BITS_ID;
    uint32_t     prefix   : SPN_TTCK_LO_BITS_PREFIX;
    uint32_t     escape   : SPN_TTCK_LO_BITS_ESCAPE;
    uint32_t     layer_lo : SPN_TTCK_LO_BITS_LAYER;
    uint32_t     layer_hi : SPN_TTCK_HI_BITS_LAYER;
    uint32_t     y        : SPN_TTCK_HI_BITS_Y;
    uint32_t     x        : SPN_TTCK_HI_BITS_X;
  };

  struct {
    uint64_t     na0      : SPN_TAGGED_BLOCK_ID_BITS_ID;
    uint64_t     na1      : SPN_TTCK_LO_BITS_PREFIX;
    uint64_t     na2      : SPN_TTCK_LO_BITS_ESCAPE;
    uint64_t     layer    : SPN_TTCK_LO_HI_BITS_LAYER;
    uint64_t     na3      : SPN_TTCK_HI_BITS_Y;
    uint64_t     na4      : SPN_TTCK_HI_BITS_X;
  };
};

//
// TTS
//

#ifndef SPN_TTS_V2

union spn_tts
{
  uint32_t   u32;

  struct {
    uint32_t tx  : SPN_TTS_BITS_TX;
    uint32_t dx  : SPN_TTS_BITS_DX;
    uint32_t ty  : SPN_TTS_BITS_TY;
    int32_t  dy  : SPN_TTS_BITS_DY;
  };

  struct {
    uint32_t txs : SPN_TTS_SUBPIXEL_X_LOG2;
    uint32_t txp : SPN_TTS_PIXEL_X_LOG2;
    uint32_t     : SPN_TTS_BITS_DX;
    uint32_t tys : SPN_TTS_SUBPIXEL_Y_LOG2;
    uint32_t typ : SPN_TTS_PIXEL_Y_LOG2;
    int32_t      : SPN_TTS_BITS_DY;
  };
};

#else

union spn_tts
{
  uint32_t   u32;

  struct {
    uint32_t tx : SPN_TTS_BITS_TX;
    int32_t  dx : SPN_TTS_BITS_DX;
    uint32_t ty : SPN_TTS_BITS_TY;
    int32_t  dy : SPN_TTS_BITS_DY;
  };
};

#endif

//
// STYLING
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
