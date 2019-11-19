// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transform_stack.h"

//
//
//

#include <math.h>
#include <stdlib.h>

//
//
//

#undef NDEBUG
#include <assert.h>

//
// FIXME(allanmac):
//
// Consider providing typed min/max() functions:
//
//   <type> [min|max]_<type>(a,b) { ; }
//
// But note we still need preprocessor-time min/max().
//

#define TS_MAX_MACRO(t, a, b) (((a) > (b)) ? (a) : (b))
#define TS_MIN_MACRO(t, a, b) (((a) < (b)) ? (a) : (b))

//
// clang-format off
//

#ifndef TRANSFORM_STACK_FLOAT_IS_DOUBLE

#define TRANSFORM_STACK_SIN(x_)  sinf(x_)
#define TRANSFORM_STACK_COS(x_)  cosf(x_)
#define TRANSFORM_STACK_TAN(x_)  tanf(x_)

#else

#define TRANSFORM_STACK_SIN(x_)  sin(x_)
#define TRANSFORM_STACK_COS(x_)  cos(x_)
#define TRANSFORM_STACK_TAN(x_)  tan(x_)

#endif

//
//
//

#define TRANSFORM_STACK_ZERO     ((transform_stack_float_t)0.0)
#define TRANSFORM_STACK_ONE      ((transform_stack_float_t)1.0)
#define TRANSFORM_STACK_RCP(f_)  (TRANSFORM_STACK_ONE / (f_))

//
//
//

union transform_stack_3x3_u
{
  transform_stack_float_t a8[8];

  struct
  {
    transform_stack_float_t sx;
    transform_stack_float_t shx;
    transform_stack_float_t tx;

    transform_stack_float_t shy;
    transform_stack_float_t sy;
    transform_stack_float_t ty;

    transform_stack_float_t w0;
    transform_stack_float_t w1;
    // w2 is always 1.0
  };

  struct
  {
    transform_stack_float_t a;
    transform_stack_float_t b;
    transform_stack_float_t c;

    transform_stack_float_t d;
    transform_stack_float_t e;
    transform_stack_float_t f;

    transform_stack_float_t g;
    transform_stack_float_t h;
    // i is always 1.0
  };
};

//
//
//

struct transform_stack
{
  uint32_t                      size;
  uint32_t                      count;

  spn_transform_weakref_t     * weakrefs;
  union transform_stack_3x3_u * transforms;
};

//
// clang-format on
//

static void
transform_stack_resize(struct transform_stack * const ts, uint32_t const size)
{
  ts->size       = size;
  ts->weakrefs   = realloc(ts->weakrefs, size * sizeof(*ts->weakrefs));
  ts->transforms = realloc(ts->transforms, size * sizeof(*ts->transforms));
}

static void
transform_stack_ensure(struct transform_stack * const ts)
{
  if (ts->count < ts->size)
    return;

  // increase by 50% and by at least 8
  uint32_t const pad = ts->size / 2;

  transform_stack_resize(ts, ts->size + TS_MAX_MACRO(uint32_t, pad, 8));
}

//
//
//

struct transform_stack *
transform_stack_create(uint32_t const size)
{
  struct transform_stack * ts = malloc(sizeof(*ts));

  ts->size  = size;
  ts->count = 0;

  ts->transforms = NULL;
  ts->weakrefs   = NULL;

  transform_stack_resize(ts, size);

  return ts;
}

void
transform_stack_release(struct transform_stack * const ts)
{
  free(ts->transforms);
  free(ts->weakrefs);

  free(ts);
}

//
//
//

uint32_t
transform_stack_save(struct transform_stack * const ts)
{
  return ts->count;
}

void
transform_stack_restore(struct transform_stack * const ts, uint32_t const restore)
{
  ts->count = restore;
}

//
//
//

static union transform_stack_3x3_u *
transform_stack_tos(struct transform_stack * const ts)
{
  return ts->transforms + ts->count - 1;
}

//
//
//

static void
transform_stack_3x3_u_copy(union transform_stack_3x3_u * const __restrict dst,
                           union transform_stack_3x3_u const * const __restrict src)
{
  *dst = *src;
}

//
// C = A * B
//
// FIXME -- can save affine vs. projective flags and save a few ops
//

//
// clang-format off
//

#define TRANSFORM_STACK_MULTIPLY(A,B)                                 \
  A->sx  * B->sx   +  A->shx * B->shy  +  A->tx * B->w0,              \
  A->sx  * B->shx  +  A->shx * B->sy   +  A->tx * B->w1,              \
  A->sx  * B->tx   +  A->shx * B->ty   +  A->tx,                      \
  A->shy * B->sx   +  A->sy  * B->shy  +  A->ty * B->w0,              \
  A->shy * B->shx  +  A->sy  * B->sy   +  A->ty * B->w1,              \
  A->shy * B->tx   +  A->sy  * B->ty   +  A->ty,                      \
  A->w0  * B->sx   +  A->w1  * B->shy  +          B->w0,              \
  A->w0  * B->shx  +  A->w1  * B->sy   +          B->w1,              \
  A->w0  * B->tx   +  A->w1  * B->ty   +  TRANSFORM_STACK_ONE

//
// clang-format on
//

#define TRANSFORM_STACK_IS_AFFINE(t_)                                                              \
  ((t_->w0 == TRANSFORM_STACK_ZERO) && (t_->w1 == TRANSFORM_STACK_ZERO))

static transform_stack_entry_e
transform_stack_classify(struct transform_stack * const ts)
{
  union transform_stack_3x3_u const * const t = transform_stack_tos(ts);

  if (TRANSFORM_STACK_IS_AFFINE(t))
    return TRANSFORM_STACK_ENTRY_AFFINE;
  else
    return TRANSFORM_STACK_ENTRY_PROJECTIVE;
}

//
//
//

transform_stack_float_t *
transform_stack_top_transform(struct transform_stack * const ts)
{
  return transform_stack_tos(ts)->a8;
}

spn_transform_weakref_t *
transform_stack_top_weakref(struct transform_stack * const ts)
{
  return ts->weakrefs + ts->count - 1;
}

//
//
//

void
transform_stack_dup(struct transform_stack * const ts)
{
  transform_stack_ensure(ts);

  union transform_stack_3x3_u * const tos = transform_stack_tos(ts);

  transform_stack_3x3_u_copy(tos + 1, tos);

  ts->weakrefs[ts->count] = ts->weakrefs[ts->count - 1];

  ts->count += 1;
}

void
transform_stack_drop(struct transform_stack * const ts)
{
  assert(ts->count >= 1);

  ts->count -= 1;
}

//
//
//

#if 0  // NOTE(allanmac): WILL BE USED IN THE FUTURE

static void
transform_stack_swap_drop(struct transform_stack * const ts)
{
  assert(ts->count >= 2);

  union transform_stack_3x3_u * const tos = transform_stack_tos(ts);

  transform_stack_3x3_u_copy(tos - 1, tos);

  ts->weakrefs[ts->count - 2] = ts->weakrefs[ts->count - 1];

  ts->count -= 1;
}

#endif

//
//
//

static void
transform_stack_store_matrix_8(struct transform_stack * const ts,
                               uint32_t const                 idx,
                               transform_stack_float_t const  sx,
                               transform_stack_float_t const  shx,
                               transform_stack_float_t const  tx,
                               transform_stack_float_t const  shy,
                               transform_stack_float_t const  sy,
                               transform_stack_float_t const  ty,
                               transform_stack_float_t const  w0,
                               transform_stack_float_t const  w1)
{
  union transform_stack_3x3_u * t = ts->transforms + idx;

  t->sx  = sx;
  t->shx = shx;
  t->tx  = tx;
  t->shy = shy;
  t->sy  = sy;
  t->ty  = ty;
  t->w0  = w0;
  t->w1  = w1;

  ts->weakrefs[idx] = SPN_TRANSFORM_WEAKREF_INVALID;
}

//
//
//

static void
transform_stack_store_matrix(struct transform_stack * const ts,
                             uint32_t const                 idx,
                             transform_stack_float_t const  sx,
                             transform_stack_float_t const  shx,
                             transform_stack_float_t const  tx,
                             transform_stack_float_t const  shy,
                             transform_stack_float_t const  sy,
                             transform_stack_float_t const  ty,
                             transform_stack_float_t const  w0,
                             transform_stack_float_t const  w1,
                             transform_stack_float_t const  w2)
{
  if (w2 == TRANSFORM_STACK_ONE)
    {
      transform_stack_store_matrix_8(ts, idx, sx, shx, tx, shy, sy, ty, w0, w1);
    }
  else
    {
      // normalize
      transform_stack_float_t d = TRANSFORM_STACK_RCP(w2);

      transform_stack_store_matrix_8(ts,
                                     idx,
                                     sx * d,
                                     shx * d,
                                     tx * d,
                                     shy * d,
                                     sy * d,
                                     ty * d,
                                     w0 * d,
                                     w1 * d);
    }
}

//
//
//

static void
transform_stack_push_matrix_8(struct transform_stack * const ts,
                              transform_stack_float_t const  sx,
                              transform_stack_float_t const  shx,
                              transform_stack_float_t const  tx,
                              transform_stack_float_t const  shy,
                              transform_stack_float_t const  sy,
                              transform_stack_float_t const  ty,
                              transform_stack_float_t const  w0,
                              transform_stack_float_t const  w1)
{
  transform_stack_ensure(ts);

  transform_stack_store_matrix_8(ts, ts->count++, sx, shx, tx, shy, sy, ty, w0, w1);
}

//
//
//

void
transform_stack_push_matrix(struct transform_stack * const ts,
                            transform_stack_float_t const  sx,
                            transform_stack_float_t const  shx,
                            transform_stack_float_t const  tx,
                            transform_stack_float_t const  shy,
                            transform_stack_float_t const  sy,
                            transform_stack_float_t const  ty,
                            transform_stack_float_t const  w0,
                            transform_stack_float_t const  w1,
                            transform_stack_float_t const  w2)
{
  if (w2 == TRANSFORM_STACK_ONE)
    {
      transform_stack_push_matrix_8(ts, sx, shx, tx, shy, sy, ty, w0, w1);
    }
  else
    {
      // normalize
      transform_stack_float_t d = TRANSFORM_STACK_RCP(w2);

      transform_stack_push_matrix_8(ts,
                                    sx * d,
                                    shx * d,
                                    tx * d,
                                    shy * d,
                                    sy * d,
                                    ty * d,
                                    w0 * d,
                                    w1 * d);
    }
}

//
//
//

void
transform_stack_push_identity(struct transform_stack * const ts)
{
  transform_stack_push_matrix_8(ts, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
}

void
transform_stack_push_affine(struct transform_stack * const ts,
                            transform_stack_float_t const  sx,
                            transform_stack_float_t const  shx,
                            transform_stack_float_t const  tx,
                            transform_stack_float_t const  shy,
                            transform_stack_float_t const  sy,
                            transform_stack_float_t const  ty)
{
  transform_stack_push_matrix_8(ts, sx, shx, tx, shy, sy, ty, 0.0, 0.0);
}

void
transform_stack_push_translate(struct transform_stack * const ts,
                               transform_stack_float_t const  tx,
                               transform_stack_float_t const  ty)
{
  transform_stack_push_matrix_8(ts, 1.0, 0.0, tx, 0.0, 1.0, ty, 0.0, 0.0);
}

void
transform_stack_push_scale(struct transform_stack * const ts,
                           transform_stack_float_t const  sx,
                           transform_stack_float_t const  sy)
{
  transform_stack_push_matrix_8(ts, sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0);
}

void
transform_stack_push_shear(struct transform_stack * const ts,
                           transform_stack_float_t const  shx,
                           transform_stack_float_t const  shy)
{
  transform_stack_push_matrix_8(ts, 1.0, shx, 0.0, shy, 1.0, 0.0, 0.0, 0.0);
}

void
transform_stack_push_skew_x(struct transform_stack * const ts, transform_stack_float_t const theta)
{
  // FIXME(allanmac): replace with tanpi if available
  transform_stack_float_t const tan_theta = TRANSFORM_STACK_TAN(theta);

  transform_stack_push_matrix_8(ts, 1.0, tan_theta, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
}

void
transform_stack_push_skew_y(struct transform_stack * const ts, transform_stack_float_t const theta)
{
  // FIXME(allanmac): replace with tanpi if available
  transform_stack_float_t const tan_theta = TRANSFORM_STACK_TAN(theta);

  transform_stack_push_matrix_8(ts, 1.0, 0.0, 0.0, tan_theta, 1.0, 0.0, 0.0, 0.0);
}

void
transform_stack_push_rotate(struct transform_stack * const ts, transform_stack_float_t const theta)
{
  // FIXME(allanmac): replace with cospi if available
  transform_stack_float_t const cos_theta = TRANSFORM_STACK_COS(theta);
  // FIXME(allanmac): replace with sinpi if available
  transform_stack_float_t const sin_theta = TRANSFORM_STACK_SIN(theta);

  transform_stack_push_matrix_8(ts,
                                cos_theta,
                                -sin_theta,
                                0.0,
                                sin_theta,
                                cos_theta,
                                0.0,
                                0.0,
                                0.0);
}

void
transform_stack_push_rotate_xy2(struct transform_stack * const ts,
                                transform_stack_float_t const  theta,
                                transform_stack_float_t const  cx,
                                transform_stack_float_t const  cy,
                                transform_stack_float_t const  tx,
                                transform_stack_float_t const  ty)
{
  // FIXME(allanmac): replace with cospi if available
  transform_stack_float_t const cos_theta = TRANSFORM_STACK_COS(theta);
  // FIXME(allanmac): replace with sinpi if available
  transform_stack_float_t const sin_theta = TRANSFORM_STACK_SIN(theta);

  transform_stack_push_matrix_8(ts,
                                cos_theta,
                                -sin_theta,
                                tx - (cx * cos_theta) + (cy * sin_theta),
                                sin_theta,
                                cos_theta,
                                ty - (cx * sin_theta) - (cy * cos_theta),
                                0.0,
                                0.0);
}

void
transform_stack_push_rotate_xy(struct transform_stack * const ts,
                               transform_stack_float_t const  theta,
                               transform_stack_float_t const  cx,
                               transform_stack_float_t const  cy)
{
  transform_stack_push_rotate_xy2(ts, theta, cx, cy, cx, cy);
}

void
transform_stack_push_rotate_scale_xy(struct transform_stack * const ts,
                                     transform_stack_float_t const  theta,
                                     transform_stack_float_t const  sx,
                                     transform_stack_float_t const  sy,
                                     transform_stack_float_t const  cx,
                                     transform_stack_float_t const  cy)
{
  // FIXME(allanmac): replace with cospi if available
  transform_stack_float_t const cos_theta = TRANSFORM_STACK_COS(theta);
  // FIXME(allanmac): replace with sinpi if available
  transform_stack_float_t const sin_theta = TRANSFORM_STACK_SIN(theta);

  transform_stack_push_matrix_8(ts,
                                sx * cos_theta,
                                -sx * sin_theta,
                                cx - cx * sx * cos_theta + cy * sy * sin_theta,
                                sy * sin_theta,
                                sy * cos_theta,
                                cy - cy * sy * cos_theta - cx * sx * sin_theta,
                                0.0,
                                0.0);
}

//
// See: "Fundamentals of Texture Mapping and Image Warping" by Paul S. Heckbert (1989)
//

#define DET(a, b, c, d) (a * d - b * c)

#define X(v, i) v[i * 2]
#define Y(v, i) v[i * 2 + 1]

//
//
//

transform_stack_entry_e
transform_stack_adjoint(struct transform_stack * const ts)
{
  union transform_stack_3x3_u * const t = transform_stack_tos(ts);

#if 0
  // save for determinant
  transform_stack_float_t const a = t->a;
  transform_stack_float_t const b = t->b;
  transform_stack_float_t const c = t->c;
#endif

  transform_stack_store_matrix(ts,
                               ts->count - 1,

                               +DET(t->e, t->f, t->h, TRANSFORM_STACK_ONE),
                               -DET(t->b, t->c, t->h, TRANSFORM_STACK_ONE),
                               +DET(t->b, t->c, t->e, t->f),

                               -DET(t->d, t->f, t->g, TRANSFORM_STACK_ONE),
                               +DET(t->a, t->c, t->g, TRANSFORM_STACK_ONE),
                               -DET(t->a, t->c, t->d, t->f),

                               +DET(t->d, t->e, t->g, t->h),
                               -DET(t->a, t->b, t->g, t->h),
                               +DET(t->a, t->b, t->d, t->e));

#if 0
  // determinant of t
  transform_stack_float_t const det = a * t->a + b * t->d + c * t->g;
#endif

  return transform_stack_classify(ts);
}

//
//
//

transform_stack_entry_e
transform_stack_push_unit_to_quad(struct transform_stack * const ts,
                                  transform_stack_float_t const  quad[8])
{
  transform_stack_float_t const x0 = X(quad, 0);
  transform_stack_float_t const y0 = Y(quad, 0);

  transform_stack_float_t const x1 = X(quad, 1);
  transform_stack_float_t const y1 = Y(quad, 1);

  transform_stack_float_t const x2 = X(quad, 2);
  transform_stack_float_t const y2 = Y(quad, 2);

  transform_stack_float_t const x3 = X(quad, 3);
  transform_stack_float_t const y3 = Y(quad, 3);

  transform_stack_float_t sx  = x1 - x0;
  transform_stack_float_t shy = y1 - y0;

  transform_stack_float_t const dx2 = x3 - x2;
  transform_stack_float_t const dy2 = y3 - y2;

  transform_stack_float_t const dx3 = -sx - dx2;
  transform_stack_float_t const dy3 = -shy - dy2;

  // if both zero then quad_dst is a parallelogram and affine
  if ((dx3 == TRANSFORM_STACK_ZERO) && (dy3 == TRANSFORM_STACK_ZERO))
    {
      transform_stack_float_t const shx = x2 - x1;
      transform_stack_float_t const sy  = y2 - y1;

      transform_stack_push_matrix_8(ts, sx, shx, x0, shy, sy, y0, 0.0, 0.0);

      return TRANSFORM_STACK_ENTRY_AFFINE;
    }
  else
    {
      transform_stack_float_t const dx1 = x1 - x2;
      transform_stack_float_t const dy1 = y1 - y2;

      transform_stack_float_t const wx_den = dx1 * dy2 - dx2 * dy1;

      if (wx_den == TRANSFORM_STACK_ZERO)
        return TRANSFORM_STACK_ENTRY_INVALID;

      transform_stack_float_t const w0_num = dx3 * dy2 - dx2 * dy3;
      transform_stack_float_t const w1_num = dx1 * dy3 - dx3 * dy1;

      transform_stack_float_t const w0 = w0_num / wx_den;
      transform_stack_float_t const w1 = w1_num / wx_den;

      sx += w0 * x1;
      transform_stack_float_t const shx = x3 - x0 + w1 * x3;

      shy += w0 * y1;
      transform_stack_float_t const sy = y3 - y0 + w1 * y3;

      transform_stack_push_matrix_8(ts, sx, shx, x0, shy, sy, y0, w0, w1);

      return TRANSFORM_STACK_ENTRY_PROJECTIVE;
    }
}

//
//
//

transform_stack_entry_e
transform_stack_push_quad_to_unit(struct transform_stack * const ts, float const quad[8])
{
  if (transform_stack_push_unit_to_quad(ts, quad) == TRANSFORM_STACK_ENTRY_INVALID)
    return TRANSFORM_STACK_ENTRY_INVALID;

  return transform_stack_adjoint(ts);
}

//
//
//

transform_stack_entry_e
transform_stack_push_quad_to_quad(struct transform_stack * const ts,
                                  transform_stack_float_t const  quad_src[8],
                                  transform_stack_float_t const  quad_dst[8])
{
  if (transform_stack_push_unit_to_quad(ts, quad_dst) == TRANSFORM_STACK_ENTRY_INVALID)
    return TRANSFORM_STACK_ENTRY_INVALID;

  if (transform_stack_push_quad_to_unit(ts, quad_src) == TRANSFORM_STACK_ENTRY_INVALID)
    return TRANSFORM_STACK_ENTRY_INVALID;

  transform_stack_multiply(ts);

  return transform_stack_classify(ts);
}

//
//
//

transform_stack_entry_e
transform_stack_push_rect_to_quad(struct transform_stack * const ts,
                                  transform_stack_float_t const  x0,
                                  transform_stack_float_t const  y0,
                                  transform_stack_float_t const  x1,
                                  transform_stack_float_t const  y1,
                                  transform_stack_float_t const  quad_dst[8])
{
  if (transform_stack_push_unit_to_quad(ts, quad_dst) == TRANSFORM_STACK_ENTRY_INVALID)
    return TRANSFORM_STACK_ENTRY_INVALID;

  transform_stack_push_matrix_8(ts,
                                TRANSFORM_STACK_RCP(x1 - x0),
                                0.0,
                                -x0,
                                0.0,
                                TRANSFORM_STACK_RCP(y1 - y0),
                                -y0,
                                0.0,
                                0.0);

  transform_stack_multiply(ts);

  return transform_stack_classify(ts);
}

//
// The second matrix on the stack (TOS[-1]) is post-multiplied by the
// top matrix on the stack (TOS[0]).
//
// The result replaces TOS[0] and TOS[-1] is unmodified.
//
// The stack effect of concat is:
//
//   | B |    | A*B |
//   | A |    |  A  |
//   | . | => |  .  |
//   | . |    |  .  |
//   | . |    |  .  |
//
void
transform_stack_concat(struct transform_stack * const ts)
{
  assert(ts->count >= 2);

  // get A and B
  union transform_stack_3x3_u const * const B = transform_stack_tos(ts);
  union transform_stack_3x3_u const * const A = B - 1;

  transform_stack_store_matrix(ts, ts->count - 1, TRANSFORM_STACK_MULTIPLY(A, B));
}

//
// The second matrix on the stack (TOS[-1]) is post-multiplied by the
// top matrix on the stack (TOS[0]).
//
// The result replaces both matrices.
//
// The stack effect of multiply is:
//
//   | B |    | A*B |
//   | A |    |  .  |
//   | . | => |  .  |
//   | . |    |  .  |
//   | . |    |  .  |
//
void
transform_stack_multiply(struct transform_stack * const ts)
{
  assert(ts->count >= 2);

  // get A and B
  union transform_stack_3x3_u const * const B = transform_stack_tos(ts);
  union transform_stack_3x3_u const * const A = B - 1;

  transform_stack_store_matrix(ts, ts->count-- - 2, TRANSFORM_STACK_MULTIPLY(A, B));
}

//
//
//

void
transform_stack_transform_xy(struct transform_stack * const  ts,
                             transform_stack_float_t const   x,
                             transform_stack_float_t const   y,
                             transform_stack_float_t * const xp,
                             transform_stack_float_t * const yp)
{
  union transform_stack_3x3_u const * const t = transform_stack_tos(ts);

  *xp = x * t->sx + y * t->shx + t->tx;
  *yp = x * t->shy + y * t->sy + t->ty;

  if (!TRANSFORM_STACK_IS_AFFINE(t))
    {
      transform_stack_float_t const d =
        TRANSFORM_STACK_RCP(x * t->w0 + y * t->w1 + TRANSFORM_STACK_ONE);

      *xp *= d;
      *yp *= d;
    }
}

//
// test it!
//

#ifdef TRANSFORM_STACK_DEBUG

#include <stdio.h>

#define TRANSFORM_STACK_SCALE 32.0

//
//
//

void
transform_stack_tos_debug(struct transform_stack * const ts)
{
  union transform_stack_3x3_u const * const t = transform_stack_tos(ts);

  printf("{ { %13.5f, %13.5f, %13.5f },\n"
         "  { %13.5f, %13.5f, %13.5f },\n"
         "  { %13.5f, %13.5f, %13.5f } }\n",
         t->a8[0],
         t->a8[1],
         t->a8[2],
         t->a8[3],
         t->a8[4],
         t->a8[5],
         t->a8[6],
         t->a8[7],
         TRANSFORM_STACK_ONE);
}

//
//
//

void
transform_stack_debug(struct transform_stack * const ts, transform_stack_float_t const quad[8])
{
  transform_stack_tos_debug(ts);

  for (int ii = 0; ii < 8; ii += 2)
    {
      transform_stack_float_t xp, yp;

      transform_stack_transform_xy(ts, quad[ii], quad[ii + 1], &xp, &yp);

      printf("( %13.2f, %13.2f ) \t-> ( %13.2f, %13.2f )\n",
             xp,
             yp,
             xp / TRANSFORM_STACK_SCALE,
             yp / TRANSFORM_STACK_SCALE);
    }
}

//
//
//

int
main(int argc, char * argv[])
{
  struct transform_stack * const ts = transform_stack_create(32);

  transform_stack_float_t const w = 1000;
  transform_stack_float_t const h = 1000;

#if 1
  transform_stack_push_scale(ts, TRANSFORM_STACK_SCALE, TRANSFORM_STACK_SCALE);

  // OpenGL'ism
  transform_stack_push_affine(ts, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, h);
  // multiply
  transform_stack_concat(ts);
#else
  transform_stack_push_identity(ts);
#endif

  uint32_t const restore = transform_stack_save(ts);

  //
  //
  //
  transform_stack_float_t const quad_src[8] = { 0.0f, 0.0f, w, 0.0f, w, h, 0.0f, h };

  transform_stack_float_t const quad_dst[8] = { 300.0f, 0.0f, w - 300.0f, 0.0f, w, h, 0.0f, h };

  transform_stack_float_t const quad_tst[8] = { 50, 50, 1550, 50, 1550, 1550, 50, 1550 };
  //
  // RECT TO QUAD
  //
  printf("type = %d\n", transform_stack_push_rect_to_quad(ts, 0.0, 0.0, w, h, quad_dst));
  transform_stack_concat(ts);

  transform_stack_debug(ts, quad_src);

  //
  // QUAD TO QUAD
  //
  transform_stack_restore(ts, restore);

  printf("type = %d\n", transform_stack_push_quad_to_quad(ts, quad_src, quad_dst));
  transform_stack_concat(ts);

  transform_stack_debug(ts, quad_src);

  //
  // DIRECT
  //
  transform_stack_restore(ts, restore);

  transform_stack_push_matrix(ts,
                              0.87004626f,
                              -0.35519487f,
                              72.14745f,
                              0.0f,
                              0.2600208f,
                              86.16314f,
                              0.0f,
                              -0.0029599573f,
                              1.0f);

  transform_stack_concat(ts);

  transform_stack_float_t const quad_foo[8] = { -10, 10, 130, 10, 130, 110, -10, 110 };

  transform_stack_debug(ts, quad_foo);

  return EXIT_SUCCESS;
}

#endif

//
//
//
