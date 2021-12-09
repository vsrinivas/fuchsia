// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_CORE_C_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_CORE_C_H_

/////////////////////////////////////////////////////////////////
//
// C99
//

#include "common/macros.h"
#include "core.h"

//
// clang-format off
//
#define SPN_BITS_TO_MASK(n)       BITS_TO_MASK_MACRO(n)
#define SPN_BITS_TO_MASK_AT(n,b)  BITS_TO_MASK_AT_MACRO(n,b)


//
// TAGGED BLOCK ID
//
typedef uint32_t spinel_tagged_block_id_t;

union spinel_tagged_block_id
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
typedef uint32_t spinel_block_id_t;

//
// PATH
//
union spinel_path_prims
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

union spinel_path_header
{
  uint32_t                  array[SPN_PATH_HEAD_DWORDS];

  struct {
    uint32_t                handle; // host handle
    uint32_t                blocks; // total number of blocks in entire path object -- includes nodes and segments
    uint32_t                nodes;  // number of trailing path node blocks -- not including head

    union spinel_path_prims prims;

    float                   bounds[4];
  } named;
};

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_CORE_C_H_
