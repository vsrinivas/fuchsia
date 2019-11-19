// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PATH_BUILDER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PATH_BUILDER_H_

//
//
//

#include "spinel/spinel.h"
#include "state_assert.h"

//
// clang-format off
//

typedef enum spn_path_builder_state_e
{
  SPN_PATH_BUILDER_STATE_READY,
  SPN_PATH_BUILDER_STATE_BUILDING
} spn_path_builder_state_e;


//
// We define all path geometry types here since their differences are
// mechanical and we may add or remove types if necessary.
//

#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()                                           \
  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(line,     SPN_BLOCK_ID_TAG_PATH_LINE     , 4)   \
  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(quad,     SPN_BLOCK_ID_TAG_PATH_QUAD     , 6)   \
  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(cubic,    SPN_BLOCK_ID_TAG_PATH_CUBIC    , 8)   \
  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(rat_quad, SPN_BLOCK_ID_TAG_PATH_RAT_QUAD , 7)   \
  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(rat_cubic,SPN_BLOCK_ID_TAG_PATH_RAT_CUBIC,10)

#define SPN_PATH_BUILDER_PRIM_TYPE_COUNT  5

//
//
//

struct spn_path_builder_coords_next
{
  struct {
#undef  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p,_i,_n)   \
    float *    _p[_n];

    SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()
  } coords;

  union {
    uint32_t   aN[SPN_PATH_BUILDER_PRIM_TYPE_COUNT];
    struct {
#undef  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p,_i,_n)   \
      uint32_t _p;

      SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()
    };
  } rem;
};

//
//
//

struct spn_path_builder
{
  struct spn_path_builder_impl      * impl;

  spn_result_t                       (* begin    )(struct spn_path_builder_impl * const impl);
  spn_result_t                       (* end      )(struct spn_path_builder_impl * const impl, spn_path_t * const path);
  spn_result_t                       (* release  )(struct spn_path_builder_impl * const impl);
  spn_result_t                       (* flush    )(struct spn_path_builder_impl * const impl);

#undef  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p,_i,_n)                                           \
  spn_result_t                       (* _p       )(struct spn_path_builder_impl * const impl);

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

  struct spn_path_builder_coords_next cn;

  struct {
    float                             x;
    float                             y;
  } curr[2];

  int32_t                             refcount;

  SPN_ASSERT_STATE_DECLARE(spn_path_builder_state_e);
};

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PATH_BUILDER_H_
