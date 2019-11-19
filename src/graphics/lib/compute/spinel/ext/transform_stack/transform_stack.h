// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_TRANSFORM_STACK_TRANSFORM_STACK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_TRANSFORM_STACK_TRANSFORM_STACK_H_

//
//
//

#include <stdint.h>

#include "spinel/spinel_types.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct transform_stack;

//
//
//

#ifndef TRANSFORM_STACK_FLOAT_IS_DOUBLE
//
// Use a 32-bit float
//
typedef float transform_stack_float_t;
#else
//
// Use a double
//
typedef double transform_stack_float_t;
#endif

//
//
//

typedef enum transform_stack_entry
{
  TRANSFORM_STACK_ENTRY_INVALID,
  TRANSFORM_STACK_ENTRY_AFFINE,
  TRANSFORM_STACK_ENTRY_PROJECTIVE
} transform_stack_entry_e;

//
//
//

struct transform_stack *
transform_stack_create(const uint32_t size);

void
transform_stack_release(struct transform_stack * const ts);

//
//
//

uint32_t
transform_stack_save(struct transform_stack * const ts);

void
transform_stack_restore(struct transform_stack * const ts, uint32_t const restore);

//
//
//

transform_stack_float_t *
transform_stack_top_transform(struct transform_stack * const ts);

spn_transform_weakref_t *
transform_stack_top_weakref(struct transform_stack * const ts);

//
//
//

void
transform_stack_dup(struct transform_stack * const ts);

void
transform_stack_drop(struct transform_stack * const ts);

//
//
//

void
transform_stack_transform_xy(struct transform_stack * const  ts,
                             transform_stack_float_t const   x,
                             transform_stack_float_t const   y,
                             transform_stack_float_t * const xp,
                             transform_stack_float_t * const yp);

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
                            transform_stack_float_t const  w2);

void
transform_stack_push_identity(struct transform_stack * const ts);

void
transform_stack_push_affine(struct transform_stack * const ts,
                            transform_stack_float_t const  sx,
                            transform_stack_float_t const  shx,
                            transform_stack_float_t const  tx,
                            transform_stack_float_t const  shy,
                            transform_stack_float_t const  sy,
                            transform_stack_float_t const  ty);

void
transform_stack_push_translate(struct transform_stack * const ts,
                               transform_stack_float_t const  tx,
                               transform_stack_float_t const  ty);

void
transform_stack_push_scale(struct transform_stack * const ts,
                           transform_stack_float_t const  sx,
                           transform_stack_float_t const  sy);

void
transform_stack_push_shear(struct transform_stack * const ts,
                           transform_stack_float_t const  shx,
                           transform_stack_float_t const  shy);

void
transform_stack_push_skew_x(struct transform_stack * const ts, transform_stack_float_t const theta);

void
transform_stack_push_skew_y(struct transform_stack * const ts, transform_stack_float_t const theta);

void
transform_stack_push_rotate(struct transform_stack * const ts, transform_stack_float_t const theta);

void
transform_stack_push_rotate_xy2(struct transform_stack * const ts,
                                transform_stack_float_t const  theta,
                                transform_stack_float_t const  cx,
                                transform_stack_float_t const  cy,
                                transform_stack_float_t const  tx,
                                transform_stack_float_t const  ty);

void
transform_stack_push_rotate_xy(struct transform_stack * const ts,
                               transform_stack_float_t const  theta,
                               transform_stack_float_t const  cx,
                               transform_stack_float_t const  cy);

void
transform_stack_push_rotate_scale_xy(struct transform_stack * const ts,
                                     transform_stack_float_t const  theta,
                                     transform_stack_float_t const  sx,
                                     transform_stack_float_t const  sy,
                                     transform_stack_float_t const  cx,
                                     transform_stack_float_t const  cy);
//
// Quadrilateral coordinates are transform_stack_float_t structs:
//
//   float2[4] = { xy0, xy1, xy2, xy3 }
//
// -or-
//
//   float[8]  = { x0, y0, x1, y1, x2, y2, x3, y3 };
//

transform_stack_entry_e
transform_stack_push_quad_to_unit(struct transform_stack * const ts,
                                  transform_stack_float_t const  quad[8]);

transform_stack_entry_e
transform_stack_push_unit_to_quad(struct transform_stack * const ts,
                                  transform_stack_float_t const  quad[8]);

transform_stack_entry_e
transform_stack_push_quad_to_quad(struct transform_stack * const ts,
                                  transform_stack_float_t const  quad_src[8],
                                  transform_stack_float_t const  quad_dst[8]);

transform_stack_entry_e
transform_stack_push_rect_to_quad(struct transform_stack * const ts,
                                  transform_stack_float_t const  x0,
                                  transform_stack_float_t const  y0,
                                  transform_stack_float_t const  x1,
                                  transform_stack_float_t const  y1,
                                  transform_stack_float_t const  quad_dst[8]);

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
transform_stack_concat(struct transform_stack * const ts);

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
transform_stack_multiply(struct transform_stack * const ts);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_TRANSFORM_STACK_TRANSFORM_STACK_H_
