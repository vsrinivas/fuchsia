// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "affine_transform.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

//
// affine_transform_t
//

const affine_transform_t affine_transform_identity = { .sx = 1., .sy = 1. };

bool
affine_transform_is_identity(affine_transform_t const * transform)
{
  return affine_transform_equal(transform, &affine_transform_identity);
}

void
affine_transform_apply(const affine_transform_t * transform, double * x, double * y)
{
  double in_x = *x;
  double in_y = *y;

  *x = transform->sx * in_x + transform->shx * in_y + transform->tx;
  *y = transform->shy * in_x + transform->sy * in_y + transform->ty;
}

void
affine_transform_apply_xy(const affine_transform_t * transform, double xy[2])
{
  affine_transform_apply(transform, xy + 0, xy + 1);
}

affine_transform_t
affine_transform_multiply_by_value(const affine_transform_t a, const affine_transform_t b)
{
  return affine_transform_multiply(&a, &b);
}

affine_transform_t
affine_transform_multiply(const affine_transform_t * a, const affine_transform_t * b)
{
  // clang-format off
  return (affine_transform_t){
      a->sx  * b->sx  + a->shx * b->shy,
      a->sx  * b->shx + a->shx * b->sy,
      a->shy * b->sx  + a->sy  * b->shy,
      a->shy * b->shx + a->sy  * b->sy,
      a->sx  * b->tx  + a->shx * b->ty  + a->tx,
      a->shy * b->tx  + a->sy  * b->ty  + a->ty,
  };
  // clang-format on
}

affine_transform_t
affine_transform_make_scale(double scale)
{
  return (affine_transform_t){
    .sx = scale,
    .sy = scale,
  };
}

affine_transform_t
affine_transform_make_scale_xy(double x_scale, double y_scale)
{
  return (affine_transform_t){
    .sx = x_scale,
    .sy = y_scale,
  };
}

affine_transform_t
affine_transform_make_rotation(double angle)
{
  return affine_transform_make_rotation_xy(angle, 0., 0.);
}

affine_transform_t
affine_transform_make_translation(double tx, double ty)
{
  return (affine_transform_t){
    .sx = 1.,
    .sy = 1.,
    .tx = tx,
    .ty = ty,
  };
}

affine_transform_t
affine_transform_make_rotation_xy(double angle, double center_x, double center_y)
{
  double sin_a = sin(angle);
  double cos_a = cos(angle);
  return (affine_transform_t){
    .sx  = cos_a,
    .shx = -sin_a,
    .shy = sin_a,
    .sy  = cos_a,
    .tx  = (center_x - center_x * cos_a + center_y * sin_a),
    .ty  = (center_y - center_x * sin_a - center_y * cos_a),
  };
}

affine_transform_t
affine_transform_make_shear_xy(double shear_x, double shear_y)
{
  return (affine_transform_t){
    .sx  = 1.0,
    .shx = shear_x,
    .shy = shear_y,
    .sy  = 1.0,
  };
}

affine_transform_t
affine_transform_make_skew_x(double angle)
{
  return (affine_transform_t){
    .sx  = 1.0,
    .shx = tan(angle),
    .sy  = 1.0,
  };
}

affine_transform_t
affine_transform_make_skew_y(double angle)
{
  return (affine_transform_t){
    .sx  = 1.0,
    .shy = tan(angle),
    .sy  = 1.0,
  };
}

//
// affine_transform_stack_t
//

#define STACK_DEFAULT_CAPACITY 8

struct affine_transform_stack
{
  uint32_t             depth;
  uint32_t             capacity;
  affine_transform_t * stack;
  affine_transform_t   stack0[STACK_DEFAULT_CAPACITY];
};

affine_transform_stack_t *
affine_transform_stack_create(void)
{
  affine_transform_stack_t * ts = malloc(sizeof(*ts));
  ts->depth                     = 1;
  ts->capacity                  = STACK_DEFAULT_CAPACITY;
  ts->stack                     = ts->stack0;
  ts->stack0[0]                 = affine_transform_identity;
  return ts;
}

void
affine_transform_stack_destroy(affine_transform_stack_t * ts)
{
  if (ts->stack != ts->stack0)
    free(ts->stack);
  free(ts);
}

uint32_t
affine_transform_stack_depth(const affine_transform_stack_t * stack)
{
  return stack->depth;
}

const affine_transform_t *
affine_transform_stack_top(const affine_transform_stack_t * ts)
{
  assert(ts->depth > 0);
  return ts->stack + ts->depth - 1;
}

void
affine_transform_stack_push(affine_transform_stack_t * ts, affine_transform_t transform)
{
  assert(ts->depth > 0);
  transform = affine_transform_multiply(&transform, affine_transform_stack_top(ts));
  affine_transform_stack_push_direct(ts, transform);
}

void
affine_transform_stack_push_direct(affine_transform_stack_t * ts, affine_transform_t transform)
{
  if (ts->depth >= ts->capacity)
    {
      uint32_t new_capacity = ts->capacity * 2;
      void *   old_stack    = (ts->stack == ts->stack0) ? NULL : ts->stack;
      ts->stack             = realloc(old_stack, new_capacity * sizeof(ts->stack[0]));
      ts->capacity          = new_capacity;
    }
  ts->stack[ts->depth++] = transform;
}

void
affine_transform_stack_pop(affine_transform_stack_t * ts)
{
  assert(ts->depth > 0);
  ts->depth--;
}

bool
affine_transform_equal(const affine_transform_t * a, const affine_transform_t * b)
{
  return a->sx == b->sx && a->shx == b->shx && a->tx == b->tx && a->shy == b->shy &&
         a->sy == b->sy && a->ty == b->ty;
}
