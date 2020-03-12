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

bool
affine_transform_equal(const affine_transform_t * a, const affine_transform_t * b)
{
  return a->sx == b->sx && a->shx == b->shx && a->tx == b->tx && a->shy == b->shy &&
         a->sy == b->sy && a->ty == b->ty;
}

bool
affine_transform_less(const affine_transform_t * a, const affine_transform_t * b)
{
#define COMPARE_MEMBER(name)                                                                       \
  if (a->name < b->name)                                                                           \
    return true;                                                                                   \
  if (a->name > b->name)                                                                           \
  return false

  COMPARE_MEMBER(sx);
  COMPARE_MEMBER(sy);
  COMPARE_MEMBER(tx);
  COMPARE_MEMBER(ty);
  COMPARE_MEMBER(shx);
  COMPARE_MEMBER(shy);

#undef COMPARE_MEMBER

  return false;
}
