// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_BOUNDS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_BOUNDS_H_

#include "svg/svg.h"
#include "tests/common/affine_transform.h"

#ifdef __cplusplus
extern "C" {
#endif

// Return an estimate of the bounds of an SVG document, after an optional
// affine transform is applied to its input geometry. |svg| is the input svg
// document and |transform| is NULL or a pointer to an affine transform.
// On exit, sets |*xmin|, |*ymin|, |*xmax| and |*ymax| to appropriate values.
//
// Note that an empty document will return values where
// |*xmin > *xmax && *ymin > *ymax|.
//
extern void
svg_estimate_bounds(const struct svg *         svg,
                    const affine_transform_t * transform,
                    double *                   xmin,
                    double *                   ymin,
                    double *                   xmax,
                    double *                   ymax);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_BOUNDS_H_
