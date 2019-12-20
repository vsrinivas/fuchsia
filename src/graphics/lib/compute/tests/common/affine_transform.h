// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_AFFINE_TRANSFORM_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_AFFINE_TRANSFORM_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// affine_transform_t
//

// A simple struct type used to model an affine transform in 2D space.
// See comment for affine_transform_apply() for layout details.
typedef struct affine_transform
{
  double sx;
  double shx;
  double shy;
  double sy;
  double tx;
  double ty;
} affine_transform_t;

// The identity transform as a constant.
extern const affine_transform_t affine_transform_identity;

// Return true iff |transform| is the identity.
extern bool
affine_transform_is_identity(affine_transform_t const * transform);

// Apply a transform to a point in 2d space.
//
// This really computes:
//    x' = sx * x + shx * y + tx
//    y' = shy * x + sy * y + ty
//
extern void
affine_transform_apply(const affine_transform_t * transform, double * x, double * y);

extern void
affine_transform_apply_xy(const affine_transform_t * transform, double xy[2]);

// Compute the result of a * b.
//
// Note that this takes pointer arguments, but returns a value, this allows safe
// modifications to the inputs as in:
//
//    my_transform = affine_transform_multiply(&my_transform, &my_transform);
//
extern affine_transform_t
affine_transform_multiply(const affine_transform_t * a, const affine_transform_t * b);

// Same as affine_transform_multiply, but takes non-pointer arguments instead.
// This amounts to generating the same machine code, but allows one to pass
// temporaries easily in C, as in:
//
//     my_transform = affine_transform_multiply_by_value(
//                       affine_transform_make_rotation(my_angle),
//                       affine_transform_make_translation(my_delta_x, my_delta_y));
//
extern affine_transform_t
affine_transform_multiply_by_value(const affine_transform_t a, const affine_transform_t b);

// Create a translation transform.
extern affine_transform_t
affine_transform_make_translation(double tx, double ty);

// Create a uniform scaling transform.
extern affine_transform_t
affine_transform_make_scale(double scale);

// Create a non-uniform scaling transform.
extern affine_transform_t
affine_transform_make_scale_xy(double x_scale, double y_scale);

// Create a rotation transform (around the origin).
extern affine_transform_t
affine_transform_make_rotation(double angle);

// Create a rotation transform (around a given center point).
extern affine_transform_t
affine_transform_make_rotation_xy(double angle, double center_x, double center_y);

// Create a non-uniform shearing transform.
extern affine_transform_t
affine_transform_make_shear_xy(double shear_x, double shear_y);

// Create an horizontal skewing transform.
extern affine_transform_t
affine_transform_make_skew_x(double angle);

// Create a vertical skewing transform.
extern affine_transform_t
affine_transform_make_skew_y(double angle);

// Return true if |a| and |b| are identical.
extern bool
affine_transform_equal(const affine_transform_t * a, const affine_transform_t * b);

//
// affine_transform_stack_t
//

// An opaque type used to model a stack of transforms, useful to operate
// nested transformations in 2D space, e.g. when processing vector documents or
// graphical hierarchies.
typedef struct affine_transform_stack affine_transform_stack_t;

// Create a new affine_transform_stack_t instance.
// It will have a depht of 1, with identity as the current top.
extern affine_transform_stack_t *
affine_transform_stack_create(void);

// Return the current depth of a transform stack.
extern uint32_t
affine_transform_stack_depth(const affine_transform_stack_t * stack);

// Destroy a given double transform stack instance.
extern void
affine_transform_stack_destroy(affine_transform_stack_t * stack);

// Return a const pointer to the transform at the top of the stack
// Asserts if the stack is empty.
extern const affine_transform_t *
affine_transform_stack_top(const affine_transform_stack_t * stack);

// Push a new transform on top of the stack, after multiplying it with the
// current stack top. Asserts if the stack is empty.
extern void
affine_transform_stack_push(affine_transform_stack_t * stack, affine_transform_t transform);

// Push a new transform directly on top of the stack, ignores previous entries.
extern void
affine_transform_stack_push_direct(affine_transform_stack_t * stack, affine_transform_t transform);

// Pop the top-most transform from the stack. asserts if the stack is empty.
extern void
affine_transform_stack_pop(affine_transform_stack_t * stack);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_AFFINE_TRANSFORM_H_
