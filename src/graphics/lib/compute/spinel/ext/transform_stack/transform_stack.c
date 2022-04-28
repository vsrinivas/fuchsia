// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel/ext/transform_stack/transform_stack.h"

//
//
//

#include <assert.h>
#include <math.h>
#include <stdlib.h>

//
//
//

#define SPN_TS_MAX_MACRO(t, a, b) (((a) > (b)) ? (a) : (b))
#define SPN_TS_MIN_MACRO(t, a, b) (((a) < (b)) ? (a) : (b))

//
// clang-format off
//

#define SPN_TRANSFORM_STACK_SIN(x_)  sinf(x_)
#define SPN_TRANSFORM_STACK_COS(x_)  cosf(x_)
#define SPN_TRANSFORM_STACK_TAN(x_)  tanf(x_)

//
//
//

#define SPN_TRANSFORM_STACK_ZERO     0.0f
#define SPN_TRANSFORM_STACK_ONE      1.0f
#define SPN_TRANSFORM_STACK_RCP(f_)  (SPN_TRANSFORM_STACK_ONE / (f_))

//
//
//

union spinel_transform_stack_3x3_u
{
  float a8[8];

  struct {
    float sx;
    float shx;
    float tx;
    float shy;
    float sy;
    float ty;
    float w0;
    float w1;
    // w2 is always 1.0
  };

  struct
  {
    float a;
    float b;
    float c;

    float d;
    float e;
    float f;

    float g;
    float h;
    // i is always 1.0
  };

  spinel_transform_t t;
};

//
//
//

struct spinel_transform_stack
{
  uint32_t                             size;
  uint32_t                             count;

  spinel_transform_weakref_t         * weakrefs;
  union spinel_transform_stack_3x3_u * transforms;
};

//
// clang-format on
//

static void
spinel_transform_stack_resize(struct spinel_transform_stack * ts, uint32_t size)
{
  ts->size       = size;
  ts->weakrefs   = realloc(ts->weakrefs, size * sizeof(*ts->weakrefs));
  ts->transforms = realloc(ts->transforms, size * sizeof(*ts->transforms));
}

static void
spinel_transform_stack_ensure(struct spinel_transform_stack * ts)
{
  if (ts->count < ts->size)
    return;

  // increase by 50% and by at least 8
  uint32_t const pad = ts->size / 2;

  spinel_transform_stack_resize(ts, ts->size + SPN_TS_MAX_MACRO(uint32_t, pad, 8));
}

//
//
//

struct spinel_transform_stack *
spinel_transform_stack_create(uint32_t size)
{
  struct spinel_transform_stack * ts = malloc(sizeof(*ts));

  ts->size  = size;
  ts->count = 0;

  ts->transforms = NULL;
  ts->weakrefs   = NULL;

  spinel_transform_stack_resize(ts, size);

  return ts;
}

void
spinel_transform_stack_release(struct spinel_transform_stack * ts)
{
  free(ts->transforms);
  free(ts->weakrefs);

  free(ts);
}

//
//
//

uint32_t
spinel_transform_stack_save(struct spinel_transform_stack * ts)
{
  return ts->count;
}

void
spinel_transform_stack_restore(struct spinel_transform_stack * ts, uint32_t restore)
{
  ts->count = restore;
}

//
//
//

static union spinel_transform_stack_3x3_u *
spinel_transform_stack_tos(struct spinel_transform_stack * ts)
{
  return ts->transforms + ts->count - 1;
}

//
//
//

static void
spinel_transform_stack_3x3_u_copy(union spinel_transform_stack_3x3_u * const __restrict dst,
                                  union spinel_transform_stack_3x3_u const * const __restrict src)
{
  *dst = *src;
}

//
// C = A * B
//
// FIXME(allanmac) -- can save affine vs. projective flags and save a few ops
//

//
// clang-format off
//

#define SPN_TRANSFORM_STACK_MULTIPLY(A,B)                                 \
  A->sx  * B->sx   +  A->shx * B->shy  +  A->tx * B->w0,              \
  A->sx  * B->shx  +  A->shx * B->sy   +  A->tx * B->w1,              \
  A->sx  * B->tx   +  A->shx * B->ty   +  A->tx,                      \
  A->shy * B->sx   +  A->sy  * B->shy  +  A->ty * B->w0,              \
  A->shy * B->shx  +  A->sy  * B->sy   +  A->ty * B->w1,              \
  A->shy * B->tx   +  A->sy  * B->ty   +  A->ty,                      \
  A->w0  * B->sx   +  A->w1  * B->shy  +          B->w0,              \
  A->w0  * B->shx  +  A->w1  * B->sy   +          B->w1,              \
  A->w0  * B->tx   +  A->w1  * B->ty   +  SPN_TRANSFORM_STACK_ONE

//
// clang-format on
//

#define SPN_TRANSFORM_STACK_IS_AFFINE(t_)                                                          \
  ((t_->w0 == SPN_TRANSFORM_STACK_ZERO) && (t_->w1 == SPN_TRANSFORM_STACK_ZERO))

static spinel_transform_stack_entry_e
spinel_transform_stack_classify(struct spinel_transform_stack * ts)
{
  union spinel_transform_stack_3x3_u const * const t = spinel_transform_stack_tos(ts);

  if (SPN_TRANSFORM_STACK_IS_AFFINE(t))
    {
      return SPN_TRANSFORM_STACK_ENTRY_AFFINE;
    }
  else
    {
      return SPN_TRANSFORM_STACK_ENTRY_PROJECTIVE;
    }
}

//
//
//

spinel_transform_t const *
spinel_transform_stack_top_transform(struct spinel_transform_stack * ts)
{
  return &spinel_transform_stack_tos(ts)->t;
}

spinel_transform_weakref_t *
spinel_transform_stack_top_weakref(struct spinel_transform_stack * ts)
{
  return ts->weakrefs + ts->count - 1;
}

//
//
//

void
spinel_transform_stack_dup(struct spinel_transform_stack * ts)
{
  spinel_transform_stack_ensure(ts);

  union spinel_transform_stack_3x3_u * const tos = spinel_transform_stack_tos(ts);

  spinel_transform_stack_3x3_u_copy(tos + 1, tos);

  ts->weakrefs[ts->count] = ts->weakrefs[ts->count - 1];

  ts->count += 1;
}

void
spinel_transform_stack_drop(struct spinel_transform_stack * ts)
{
  assert(ts->count >= 1);

  ts->count -= 1;
}

//
//
//

#if 0  // NOTE(allanmac): WILL BE USED IN THE FUTURE

static void
spinel_transform_stack_swap_drop(struct spinel_transform_stack * ts)
{
  assert(ts->count >= 2);

  union spinel_transform_stack_3x3_u * const tos = spinel_transform_stack_tos(ts);

  spinel_transform_stack_3x3_u_copy(tos - 1, tos);

  ts->weakrefs[ts->count - 2] = ts->weakrefs[ts->count - 1];

  ts->count -= 1;
}

#endif

//
//
//

static void
spinel_transform_stack_store_matrix_8(struct spinel_transform_stack * ts,
                                      uint32_t                        idx,
                                      float                           sx,
                                      float                           shx,
                                      float                           tx,
                                      float                           shy,
                                      float                           sy,
                                      float                           ty,
                                      float                           w0,
                                      float                           w1)
{
  union spinel_transform_stack_3x3_u * t = ts->transforms + idx;

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
spinel_transform_stack_store_matrix(struct spinel_transform_stack * ts,
                                    uint32_t                        idx,
                                    float                           sx,
                                    float                           shx,
                                    float                           tx,
                                    float                           shy,
                                    float                           sy,
                                    float                           ty,
                                    float                           w0,
                                    float                           w1,
                                    float                           w2)
{
  if (w2 == SPN_TRANSFORM_STACK_ONE)
    {
      spinel_transform_stack_store_matrix_8(ts, idx, sx, shx, tx, shy, sy, ty, w0, w1);
    }
  else
    {
      // normalize
      float const d = SPN_TRANSFORM_STACK_RCP(w2);

      spinel_transform_stack_store_matrix_8(ts,
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
spinel_transform_stack_push_matrix_8(struct spinel_transform_stack * ts,
                                     float                           sx,
                                     float                           shx,
                                     float                           tx,
                                     float                           shy,
                                     float                           sy,
                                     float                           ty,
                                     float                           w0,
                                     float                           w1)
{
  spinel_transform_stack_ensure(ts);

  spinel_transform_stack_store_matrix_8(ts, ts->count++, sx, shx, tx, shy, sy, ty, w0, w1);
}

//
//
//

void
spinel_transform_stack_push_matrix(struct spinel_transform_stack * ts,
                                   float                           sx,
                                   float                           shx,
                                   float                           tx,
                                   float                           shy,
                                   float                           sy,
                                   float                           ty,
                                   float                           w0,
                                   float                           w1,
                                   float                           w2)
{
  if (w2 == SPN_TRANSFORM_STACK_ONE)
    {
      spinel_transform_stack_push_matrix_8(ts, sx, shx, tx, shy, sy, ty, w0, w1);
    }
  else
    {
      // normalize
      float d = SPN_TRANSFORM_STACK_RCP(w2);

      spinel_transform_stack_push_matrix_8(ts,
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
spinel_transform_stack_push_transform(struct spinel_transform_stack * ts,
                                      spinel_transform_t const *      transform)
{
  spinel_transform_stack_ensure(ts);

  uint32_t const                       idx = ts->count++;
  union spinel_transform_stack_3x3_u * t   = ts->transforms + idx;

  t->t = *transform;

  ts->weakrefs[idx] = SPN_TRANSFORM_WEAKREF_INVALID;
}

//
//
//

void
spinel_transform_stack_push_identity(struct spinel_transform_stack * ts)
{
  spinel_transform_stack_push_matrix_8(ts, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
}

void
spinel_transform_stack_push_affine(
  struct spinel_transform_stack * ts, float sx, float shx, float tx, float shy, float sy, float ty)
{
  spinel_transform_stack_push_matrix_8(ts, sx, shx, tx, shy, sy, ty, 0.0, 0.0);
}

void
spinel_transform_stack_push_translate(struct spinel_transform_stack * ts, float tx, float ty)
{
  spinel_transform_stack_push_matrix_8(ts, 1.0, 0.0, tx, 0.0, 1.0, ty, 0.0, 0.0);
}

void
spinel_transform_stack_push_scale(struct spinel_transform_stack * ts, float sx, float sy)
{
  spinel_transform_stack_push_matrix_8(ts, sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0);
}

void
spinel_transform_stack_push_shear(struct spinel_transform_stack * ts, float shx, float shy)
{
  spinel_transform_stack_push_matrix_8(ts, 1.0, shx, 0.0, shy, 1.0, 0.0, 0.0, 0.0);
}

void
spinel_transform_stack_push_skew_x(struct spinel_transform_stack * ts, float theta)
{
  // FIXME(allanmac): replace with tanpi if available
  float const tan_theta = SPN_TRANSFORM_STACK_TAN(theta);

  spinel_transform_stack_push_matrix_8(ts, 1.0, tan_theta, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
}

void
spinel_transform_stack_push_skew_y(struct spinel_transform_stack * ts, float theta)
{
  // FIXME(allanmac): replace with tanpi if available
  float const tan_theta = SPN_TRANSFORM_STACK_TAN(theta);

  spinel_transform_stack_push_matrix_8(ts, 1.0, 0.0, 0.0, tan_theta, 1.0, 0.0, 0.0, 0.0);
}

void
spinel_transform_stack_push_rotate(struct spinel_transform_stack * ts, float theta)
{
  // FIXME(allanmac): replace with cospi if available
  float const cos_theta = SPN_TRANSFORM_STACK_COS(theta);
  // FIXME(allanmac): replace with sinpi if available
  float const sin_theta = SPN_TRANSFORM_STACK_SIN(theta);

  spinel_transform_stack_push_matrix_8(ts,
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
spinel_transform_stack_push_rotate_xy2(struct spinel_transform_stack * ts,  //
                                       float                           theta,
                                       float                           cx,
                                       float                           cy,
                                       float                           tx,
                                       float                           ty)
{
  // FIXME(allanmac): replace with cospi if available
  float const cos_theta = SPN_TRANSFORM_STACK_COS(theta);
  // FIXME(allanmac): replace with sinpi if available
  float const sin_theta = SPN_TRANSFORM_STACK_SIN(theta);

  spinel_transform_stack_push_matrix_8(ts,
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
spinel_transform_stack_push_rotate_xy(struct spinel_transform_stack * ts,
                                      float                           theta,
                                      float                           cx,
                                      float                           cy)
{
  spinel_transform_stack_push_rotate_xy2(ts, theta, cx, cy, cx, cy);
}

void
spinel_transform_stack_push_rotate_scale_xy(struct spinel_transform_stack * ts,  //
                                            float                           theta,
                                            float                           sx,
                                            float                           sy,
                                            float                           cx,
                                            float                           cy)
{
  // FIXME(allanmac): replace with cospi if available
  float const cos_theta = SPN_TRANSFORM_STACK_COS(theta);
  // FIXME(allanmac): replace with sinpi if available
  float const sin_theta = SPN_TRANSFORM_STACK_SIN(theta);

  spinel_transform_stack_push_matrix_8(ts,
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

spinel_transform_stack_entry_e
spinel_transform_stack_adjoint(struct spinel_transform_stack * ts)
{
  union spinel_transform_stack_3x3_u * const t = spinel_transform_stack_tos(ts);

#if 0
  // save for determinant
  float const a = t->a;
  float const b = t->b;
  float const c = t->c;
#endif

  spinel_transform_stack_store_matrix(ts,
                                      ts->count - 1,

                                      +DET(t->e, t->f, t->h, SPN_TRANSFORM_STACK_ONE),
                                      -DET(t->b, t->c, t->h, SPN_TRANSFORM_STACK_ONE),
                                      +DET(t->b, t->c, t->e, t->f),

                                      -DET(t->d, t->f, t->g, SPN_TRANSFORM_STACK_ONE),
                                      +DET(t->a, t->c, t->g, SPN_TRANSFORM_STACK_ONE),
                                      -DET(t->a, t->c, t->d, t->f),

                                      +DET(t->d, t->e, t->g, t->h),
                                      -DET(t->a, t->b, t->g, t->h),
                                      +DET(t->a, t->b, t->d, t->e));

#if 0
  // determinant of t
  float const det = a * t->a + b * t->d + c * t->g;
#endif

  return spinel_transform_stack_classify(ts);
}

//
//
//

spinel_transform_stack_entry_e
spinel_transform_stack_push_unit_to_quad(struct spinel_transform_stack * ts, float const quad[8])
{
  float const x0 = X(quad, 0);
  float const y0 = Y(quad, 0);

  float const x1 = X(quad, 1);
  float const y1 = Y(quad, 1);

  float const x2 = X(quad, 2);
  float const y2 = Y(quad, 2);

  float const x3 = X(quad, 3);
  float const y3 = Y(quad, 3);

  float sx  = x1 - x0;
  float shy = y1 - y0;

  float const dx2 = x3 - x2;
  float const dy2 = y3 - y2;

  float const dx3 = -sx - dx2;
  float const dy3 = -shy - dy2;

  // if both zero then quad_dst is a parallelogram and affine
  if ((dx3 == SPN_TRANSFORM_STACK_ZERO) && (dy3 == SPN_TRANSFORM_STACK_ZERO))
    {
      float const shx = x2 - x1;
      float const sy  = y2 - y1;

      spinel_transform_stack_push_matrix_8(ts, sx, shx, x0, shy, sy, y0, 0.0, 0.0);

      return SPN_TRANSFORM_STACK_ENTRY_AFFINE;
    }
  else
    {
      float const dx1 = x1 - x2;
      float const dy1 = y1 - y2;

      float const wx_den = dx1 * dy2 - dx2 * dy1;

      if (wx_den == SPN_TRANSFORM_STACK_ZERO)
        return SPN_TRANSFORM_STACK_ENTRY_INVALID;

      float const w0_num = dx3 * dy2 - dx2 * dy3;
      float const w1_num = dx1 * dy3 - dx3 * dy1;

      float const w0 = w0_num / wx_den;
      float const w1 = w1_num / wx_den;

      sx += w0 * x1;
      float const shx = x3 - x0 + w1 * x3;

      shy += w0 * y1;
      float const sy = y3 - y0 + w1 * y3;

      spinel_transform_stack_push_matrix_8(ts, sx, shx, x0, shy, sy, y0, w0, w1);

      return SPN_TRANSFORM_STACK_ENTRY_PROJECTIVE;
    }
}

//
//
//

spinel_transform_stack_entry_e
spinel_transform_stack_push_quad_to_unit(struct spinel_transform_stack * ts, float const quad[8])
{
  if (spinel_transform_stack_push_unit_to_quad(ts, quad) == SPN_TRANSFORM_STACK_ENTRY_INVALID)
    return SPN_TRANSFORM_STACK_ENTRY_INVALID;

  return spinel_transform_stack_adjoint(ts);
}

//
//
//

spinel_transform_stack_entry_e
spinel_transform_stack_push_quad_to_quad(struct spinel_transform_stack * ts,
                                         float const                     quad_src[8],
                                         float const                     quad_dst[8])
{
  if (spinel_transform_stack_push_unit_to_quad(ts, quad_dst) == SPN_TRANSFORM_STACK_ENTRY_INVALID)
    return SPN_TRANSFORM_STACK_ENTRY_INVALID;

  if (spinel_transform_stack_push_quad_to_unit(ts, quad_src) == SPN_TRANSFORM_STACK_ENTRY_INVALID)
    return SPN_TRANSFORM_STACK_ENTRY_INVALID;

  spinel_transform_stack_multiply(ts);

  return spinel_transform_stack_classify(ts);
}

//
//
//

spinel_transform_stack_entry_e
spinel_transform_stack_push_rect_to_quad(struct spinel_transform_stack * ts,
                                         float                           x0,
                                         float                           y0,
                                         float                           x1,
                                         float                           y1,
                                         float const                     quad_dst[8])
{
  if (spinel_transform_stack_push_unit_to_quad(ts, quad_dst) == SPN_TRANSFORM_STACK_ENTRY_INVALID)
    return SPN_TRANSFORM_STACK_ENTRY_INVALID;

  spinel_transform_stack_push_matrix_8(ts,
                                       SPN_TRANSFORM_STACK_RCP(x1 - x0),
                                       0.0,
                                       -x0,
                                       0.0,
                                       SPN_TRANSFORM_STACK_RCP(y1 - y0),
                                       -y0,
                                       0.0,
                                       0.0);

  spinel_transform_stack_multiply(ts);

  return spinel_transform_stack_classify(ts);
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
spinel_transform_stack_concat(struct spinel_transform_stack * ts)
{
  assert(ts->count >= 2);

  // get A and B
  union spinel_transform_stack_3x3_u const * const B = spinel_transform_stack_tos(ts);
  union spinel_transform_stack_3x3_u const * const A = B - 1;

  spinel_transform_stack_store_matrix(ts, ts->count - 1, SPN_TRANSFORM_STACK_MULTIPLY(A, B));
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
spinel_transform_stack_multiply(struct spinel_transform_stack * ts)
{
  assert(ts->count >= 2);

  // get A and B
  union spinel_transform_stack_3x3_u const * const B = spinel_transform_stack_tos(ts);
  union spinel_transform_stack_3x3_u const * const A = B - 1;

  spinel_transform_stack_store_matrix(ts, ts->count-- - 2, SPN_TRANSFORM_STACK_MULTIPLY(A, B));
}

//
//
//

void
spinel_transform_stack_transform_xy(
  struct spinel_transform_stack * ts, float x, float y, float * xp, float * yp)
{
  union spinel_transform_stack_3x3_u const * const t = spinel_transform_stack_tos(ts);

  *xp = x * t->sx + y * t->shx + t->tx;
  *yp = x * t->shy + y * t->sy + t->ty;

  if (!SPN_TRANSFORM_STACK_IS_AFFINE(t))
    {
      float const d = SPN_TRANSFORM_STACK_RCP(x * t->w0 + y * t->w1 + SPN_TRANSFORM_STACK_ONE);

      *xp *= d;
      *yp *= d;
    }
}

//
// test it!
//

#ifdef SPN_TRANSFORM_STACK_DEBUG

#include <stdio.h>

#define SPN_TRANSFORM_STACK_SCALE 32.0

//
//
//

void
spinel_transform_stack_tos_debug(struct spinel_transform_stack * ts)
{
  union spinel_transform_stack_3x3_u const * const t = spinel_transform_stack_tos(ts);

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
         SPN_TRANSFORM_STACK_ONE);
}

//
//
//

void
spinel_transform_stack_debug(struct spinel_transform_stack * ts, float const quad[8])
{
  spinel_transform_stack_tos_debug(ts);

  for (int ii = 0; ii < 8; ii += 2)
    {
      float xp, yp;

      spinel_transform_stack_transform_xy(ts, quad[ii], quad[ii + 1], &xp, &yp);

      printf("( %13.2f, %13.2f ) \t-> ( %13.2f, %13.2f )\n",
             xp,
             yp,
             xp / SPN_TRANSFORM_STACK_SCALE,
             yp / SPN_TRANSFORM_STACK_SCALE);
    }
}

//
//
//

int
main(int argc, char * argv[])
{
  struct spinel_transform_stack * const ts = spinel_transform_stack_create(32);

  float const w = 1000;
  float const h = 1000;

#if 1
  spinel_transform_stack_push_scale(ts, SPN_TRANSFORM_STACK_SCALE, SPN_TRANSFORM_STACK_SCALE);

  // OpenGL'ism
  spinel_transform_stack_push_affine(ts, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, h);
  // multiply
  spinel_transform_stack_concat(ts);
#else
  spinel_transform_stack_push_identity(ts);
#endif

  uint32_t const restore = spinel_transform_stack_save(ts);

  //
  //
  //
  float const quad_src[8] = { 0.0f, 0.0f, w, 0.0f, w, h, 0.0f, h };

  float const quad_dst[8] = { 300.0f, 0.0f, w - 300.0f, 0.0f, w, h, 0.0f, h };

  float const quad_tst[8] = { 50, 50, 1550, 50, 1550, 1550, 50, 1550 };
  //
  // RECT TO QUAD
  //
  printf("type = %d\n", spinel_transform_stack_push_rect_to_quad(ts, 0.0, 0.0, w, h, quad_dst));
  spinel_transform_stack_concat(ts);

  spinel_transform_stack_debug(ts, quad_src);

  //
  // QUAD TO QUAD
  //
  spinel_transform_stack_restore(ts, restore);

  printf("type = %d\n", spinel_transform_stack_push_quad_to_quad(ts, quad_src, quad_dst));
  spinel_transform_stack_concat(ts);

  spinel_transform_stack_debug(ts, quad_src);

  //
  // DIRECT
  //
  spinel_transform_stack_restore(ts, restore);

  spinel_transform_stack_push_matrix(ts,
                                     0.87004626f,
                                     -0.35519487f,
                                     72.14745f,
                                     0.0f,
                                     0.2600208f,
                                     86.16314f,
                                     0.0f,
                                     -0.0029599573f,
                                     1.0f);

  spinel_transform_stack_concat(ts);

  float const quad_foo[8] = { -10, 10, 130, 10, 130, 110, -10, 110 };

  spinel_transform_stack_debug(ts, quad_foo);

  return EXIT_SUCCESS;
}

#endif

//
//
//
