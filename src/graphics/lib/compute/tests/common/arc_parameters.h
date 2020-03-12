// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_ARC_PARAMETERS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_ARC_PARAMETERS_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Helper types and functions ton convert between the center and endpoint
// parameterization of elliptical arcs. For more details, see
// https://www.w3.org/TR/SVG/implnote.html

// Simple struct to hold the center parameterization of a given arc.
typedef struct
{
  double cx, cy;       // ellipse center.
  double rx, ry;       // ellipse radii.
  double phi;          // ellipse x-absis rotation.
  double theta;        // starting angle.
  double theta_delta;  // arc angle sweep.
} arc_center_parameters_t;

// Simple struct to hold the endpoint parameterization of a given arc.
typedef struct
{
  double x1, y1;
  double x2, y2;
  bool   large_arc_flag;
  bool   sweep_flag;
  double rx, ry;
  double phi;
} arc_endpoint_parameters_t;

// Compute the center parameterization of a given arc from its endpoint one.
extern arc_center_parameters_t
arc_center_parameters_from_endpoint(arc_endpoint_parameters_t params);

// Compute the endpoint parameterization of a given arc from its center one.
extern arc_endpoint_parameters_t
arc_endpoint_parameters_from_center(arc_center_parameters_t params);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_ARC_PARAMETERS_H_
