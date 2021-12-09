// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_GEOMETRY_ARC_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_GEOMETRY_ARC_H_

//
//
//

#include "spinel/spinel.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

typedef struct spinel_arc_params
{
  float x0;           // start point
  float y0;           //
  float x1;           // end point
  float y1;           //
  float rx;           // arc radii
  float ry;           //
  float cx;           // center of arc
  float cy;           //
  float phi;          // relative rotation of arc
  float theta;        // start angle of arc
  float theta_delta;  // sweep of arc
} spinel_arc_params_t;

//
// Create an arc from a valid center parameterization by emitting zero
// or more rational quads of fixed sweep and one final arc of the
// remaining sweep.
//
// A degenerate arc results in a line segment joining the start and end
// points.
//
// Arc parameters must satisfy:
//
//   * radii must be positive
//   * phi and theta are within a valid implementation defined range
//   * (2 * M_PI) < theta_delta < (2 * M_PI)
//

spinel_result_t
spinel_path_builder_arc(spinel_path_builder_t path_builder, spinel_arc_params_t const * arc_params);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_GEOMETRY_ARC_H_
