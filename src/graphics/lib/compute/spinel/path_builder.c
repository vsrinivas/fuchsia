// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "path_builder.h"

//
// Verify that prim count is in sync with macro
//

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) +1

STATIC_ASSERT_MACRO_1((0 SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()) == SPN_PATH_BUILDER_PRIM_TYPE_COUNT);

//
//
//

spn_result_t
spn_path_builder_retain(spn_path_builder_t path_builder)
{
  ++path_builder->refcount;

  return SPN_SUCCESS;
}

spn_result_t
spn_path_builder_release(spn_path_builder_t path_builder)
{
  SPN_ASSERT_STATE_ASSERT(SPN_PATH_BUILDER_STATE_READY, path_builder);

  return path_builder->release(path_builder->impl);
}

spn_result_t
spn_path_builder_flush(spn_path_builder_t path_builder)
{
  return path_builder->flush(path_builder->impl);
}

//
// PATH OPS
//

spn_result_t
spn_path_builder_begin(spn_path_builder_t path_builder)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_PATH_BUILDER_STATE_READY,
                              SPN_PATH_BUILDER_STATE_BUILDING,
                              path_builder);

  // begin the path
  return path_builder->begin(path_builder->impl);
}

spn_result_t
spn_path_builder_end(spn_path_builder_t path_builder, spn_path_t * path)
{
  SPN_ASSERT_STATE_TRANSITION(SPN_PATH_BUILDER_STATE_BUILDING,
                              SPN_PATH_BUILDER_STATE_READY,
                              path_builder);

  // update path header with proper counts
  return path_builder->end(path_builder->impl, path);
}

//
// PATH SEGMENT OPS
//

static void
spn_path_builder_move_to_1(spn_path_builder_t path_builder, float x0, float y0)
{
  path_builder->curr[0].x = x0;
  path_builder->curr[0].y = y0;
  path_builder->curr[1].x = x0;
  path_builder->curr[1].y = y0;
}

static void
spn_path_builder_move_to_2(spn_path_builder_t path_builder, float x0, float y0, float x1, float y1)
{
  path_builder->curr[0].x = x0;
  path_builder->curr[0].y = y0;
  path_builder->curr[1].x = x1;
  path_builder->curr[1].y = y1;
}

spn_result_t
spn_path_builder_move_to(spn_path_builder_t path_builder, float x0, float y0)
{
  if (!SPN_ASSERT_STATE_TEST(SPN_PATH_BUILDER_STATE_BUILDING, path_builder))
    return SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN;

  spn_path_builder_move_to_1(path_builder, x0, y0);

  return SPN_SUCCESS;
}

//
// Simplifying macros
//

#define SPN_PB_CN_COORDS_APPEND(_pb, _p, _n, _c) *_pb->cn.coords._p[_n]++ = _c

#define SPN_PB_CN_ACQUIRE(_pb, _p)                                                                 \
  {                                                                                                \
    if (_pb->cn.rem._p == 0)                                                                       \
      {                                                                                            \
        spn_result_t const err = _pb->_p(path_builder->impl);                                      \
        if (err != SPN_SUCCESS)                                                                    \
          return err;                                                                              \
      }                                                                                            \
    _pb->cn.rem._p -= 1;                                                                           \
  }

//
//
//

spn_result_t
spn_path_builder_line_to(spn_path_builder_t path_builder, float x1, float y1)
{
  if (!SPN_ASSERT_STATE_TEST(SPN_PATH_BUILDER_STATE_BUILDING, path_builder))
    return SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN;

  SPN_PB_CN_ACQUIRE(path_builder, line);

  SPN_PB_CN_COORDS_APPEND(path_builder, line, 0, path_builder->curr[0].x);
  SPN_PB_CN_COORDS_APPEND(path_builder, line, 1, path_builder->curr[0].y);
  SPN_PB_CN_COORDS_APPEND(path_builder, line, 2, x1);
  SPN_PB_CN_COORDS_APPEND(path_builder, line, 3, y1);

  spn_path_builder_move_to_1(path_builder, x1, y1);

  return SPN_SUCCESS;
}

spn_result_t
spn_path_builder_quad_to(spn_path_builder_t path_builder,  //
                         float              x1,
                         float              y1,
                         float              x2,
                         float              y2)
{
  if (!SPN_ASSERT_STATE_TEST(SPN_PATH_BUILDER_STATE_BUILDING, path_builder))
    return SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN;

  SPN_PB_CN_ACQUIRE(path_builder, quad);

  SPN_PB_CN_COORDS_APPEND(path_builder, quad, 0, path_builder->curr[0].x);
  SPN_PB_CN_COORDS_APPEND(path_builder, quad, 1, path_builder->curr[0].y);
  SPN_PB_CN_COORDS_APPEND(path_builder, quad, 2, x1);
  SPN_PB_CN_COORDS_APPEND(path_builder, quad, 3, y1);
  SPN_PB_CN_COORDS_APPEND(path_builder, quad, 4, x2);
  SPN_PB_CN_COORDS_APPEND(path_builder, quad, 5, y2);

  spn_path_builder_move_to_2(path_builder, x2, y2, x1, y1);

  return SPN_SUCCESS;
}

spn_result_t
spn_path_builder_quad_smooth_to(spn_path_builder_t path_builder,  //
                                float              x2,
                                float              y2)
{
  float const x1 = path_builder->curr[0].x * 2.0f - path_builder->curr[1].x;
  float const y1 = path_builder->curr[0].y * 2.0f - path_builder->curr[1].y;

  return spn_path_builder_quad_to(path_builder, x1, y1, x2, y2);
}

spn_result_t
spn_path_builder_cubic_to(spn_path_builder_t path_builder,  //
                          float              x1,
                          float              y1,
                          float              x2,
                          float              y2,
                          float              x3,
                          float              y3)
{
  if (!SPN_ASSERT_STATE_TEST(SPN_PATH_BUILDER_STATE_BUILDING, path_builder))
    return SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN;

  SPN_PB_CN_ACQUIRE(path_builder, cubic);

  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 0, path_builder->curr[0].x);
  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 1, path_builder->curr[0].y);
  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 2, x1);
  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 3, y1);
  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 4, x2);
  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 5, y2);
  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 6, x3);
  SPN_PB_CN_COORDS_APPEND(path_builder, cubic, 7, y3);

  spn_path_builder_move_to_2(path_builder, x3, y3, x2, y2);

  return SPN_SUCCESS;
}

spn_result_t
spn_path_builder_cubic_smooth_to(spn_path_builder_t path_builder,  //
                                 float              x2,
                                 float              y2,
                                 float              x3,
                                 float              y3)
{
  float const x1 = path_builder->curr[0].x * 2.0f - path_builder->curr[1].x;
  float const y1 = path_builder->curr[0].y * 2.0f - path_builder->curr[1].y;

  return spn_path_builder_cubic_to(path_builder, x1, y1, x2, y2, x3, y3);
}

//
//
//

spn_result_t
spn_path_builder_rat_quad_to(spn_path_builder_t path_builder,  //
                             float              x1,
                             float              y1,
                             float              x2,
                             float              y2,
                             float              w1)
{
  if (!SPN_ASSERT_STATE_TEST(SPN_PATH_BUILDER_STATE_BUILDING, path_builder))
    return SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN;

  SPN_PB_CN_ACQUIRE(path_builder, rat_quad);

  SPN_PB_CN_COORDS_APPEND(path_builder, rat_quad, 0, path_builder->curr[0].x);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_quad, 1, path_builder->curr[0].y);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_quad, 2, x1);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_quad, 3, y1);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_quad, 4, x2);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_quad, 5, y2);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_quad, 6, w1);

  spn_path_builder_move_to_1(path_builder, x2, y2);

  return SPN_SUCCESS;
}

spn_result_t
spn_path_builder_rat_cubic_to(spn_path_builder_t path_builder,
                              float              x1,
                              float              y1,
                              float              x2,
                              float              y2,
                              float              x3,
                              float              y3,
                              float              w1,
                              float              w2)
{
  if (!SPN_ASSERT_STATE_TEST(SPN_PATH_BUILDER_STATE_BUILDING, path_builder))
    return SPN_ERROR_PATH_BUILDER_PATH_NOT_BEGUN;

  SPN_PB_CN_ACQUIRE(path_builder, rat_cubic);

  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 0, path_builder->curr[0].x);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 1, path_builder->curr[0].y);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 2, x1);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 3, y1);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 4, x2);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 5, y2);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 6, x3);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 7, y3);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 8, w1);
  SPN_PB_CN_COORDS_APPEND(path_builder, rat_cubic, 9, w2);

  spn_path_builder_move_to_1(path_builder, x3, y3);

  return SPN_SUCCESS;
}

//
//
//
