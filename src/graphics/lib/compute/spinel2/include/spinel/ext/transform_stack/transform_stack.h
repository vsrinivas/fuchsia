// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_TRANSFORM_STACK_TRANSFORM_STACK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_TRANSFORM_STACK_TRANSFORM_STACK_H_

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

struct spinel_transform_stack;

//
//
//

typedef enum spinel_transform_stack_entry
{
  SPN_TRANSFORM_STACK_ENTRY_INVALID,
  SPN_TRANSFORM_STACK_ENTRY_AFFINE,
  SPN_TRANSFORM_STACK_ENTRY_PROJECTIVE
} spinel_transform_stack_entry_e;

//
//
//

struct spinel_transform_stack *
spinel_transform_stack_create(const uint32_t size);

void
spinel_transform_stack_release(struct spinel_transform_stack * ts);

//
//
//

uint32_t
spinel_transform_stack_save(struct spinel_transform_stack * ts);

void
spinel_transform_stack_restore(struct spinel_transform_stack * ts, uint32_t restore);

//
//
//

spinel_transform_t const *
spinel_transform_stack_top_transform(struct spinel_transform_stack * ts);

spinel_transform_weakref_t *
spinel_transform_stack_top_weakref(struct spinel_transform_stack * ts);

//
//
//

void
spinel_transform_stack_dup(struct spinel_transform_stack * ts);

void
spinel_transform_stack_drop(struct spinel_transform_stack * ts);

//
//
//

void
spinel_transform_stack_transform_xy(struct spinel_transform_stack * ts,  //
                                    float                           x,
                                    float                           y,
                                    float *                         xp,
                                    float *                         yp);

//
// Implicitly assumes w2=1
//

void
spinel_transform_stack_push_transform(struct spinel_transform_stack * ts,
                                      spinel_transform_t const *      transform);

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
                                   float                           w2);

//
//
//

void
spinel_transform_stack_push_affine(struct spinel_transform_stack * ts,  //
                                   float                           sx,
                                   float                           shx,
                                   float                           tx,
                                   float                           shy,
                                   float                           sy,
                                   float                           ty);

void
spinel_transform_stack_push_identity(struct spinel_transform_stack * ts);

void
spinel_transform_stack_push_translate(struct spinel_transform_stack * ts, float tx, float ty);

void
spinel_transform_stack_push_scale(struct spinel_transform_stack * ts, float sx, float sy);

void
spinel_transform_stack_push_shear(struct spinel_transform_stack * ts, float shx, float shy);

void
spinel_transform_stack_push_skew_x(struct spinel_transform_stack * ts, float theta);

void
spinel_transform_stack_push_skew_y(struct spinel_transform_stack * ts, float theta);

void
spinel_transform_stack_push_rotate(struct spinel_transform_stack * ts, float theta);

void
spinel_transform_stack_push_rotate_xy2(struct spinel_transform_stack * ts,  //
                                       float                           theta,
                                       float                           cx,
                                       float                           cy,
                                       float                           tx,
                                       float                           ty);

void
spinel_transform_stack_push_rotate_xy(struct spinel_transform_stack * ts,
                                      float                           theta,
                                      float                           cx,
                                      float                           cy);

void
spinel_transform_stack_push_rotate_scale_xy(struct spinel_transform_stack * ts,  //
                                            float                           theta,
                                            float                           sx,
                                            float                           sy,
                                            float                           cx,
                                            float                           cy);
//
// Quadrilateral coordinates are float structs:
//
//   float2[4] = { xy0, xy1, xy2, xy3 }
//
// -or-
//
//   float[8]  = { x0, y0, x1, y1, x2, y2, x3, y3 };
//

spinel_transform_stack_entry_e
spinel_transform_stack_push_quad_to_unit(struct spinel_transform_stack * ts, float const quad[8]);

spinel_transform_stack_entry_e
spinel_transform_stack_push_unit_to_quad(struct spinel_transform_stack * ts, float const quad[8]);

spinel_transform_stack_entry_e
spinel_transform_stack_push_quad_to_quad(struct spinel_transform_stack * ts,
                                         float const                     quad_src[8],
                                         float const                     quad_dst[8]);

spinel_transform_stack_entry_e
spinel_transform_stack_push_rect_to_quad(struct spinel_transform_stack * ts,
                                         float                           x0,
                                         float                           y0,
                                         float                           x1,
                                         float                           y1,
                                         float const                     quad_dst[8]);

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
spinel_transform_stack_concat(struct spinel_transform_stack * ts);

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
spinel_transform_stack_multiply(struct spinel_transform_stack * ts);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_TRANSFORM_STACK_TRANSFORM_STACK_H_
