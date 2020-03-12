// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/path_sink.h"

#include <math.h>
#include <string.h>

#include "tests/common/arc_parameters.h"

//
//
//

void
PathSink::addArcTo(double cx,
                   double cy,
                   double rx,
                   double ry,
                   double x_axis_rotation,
                   double angle,
                   double angle_delta)
{
  const double cos_phi = cos(x_axis_rotation);
  const double sin_phi = sin(x_axis_rotation);

  // Emit rational quadratic beziers in the transformed space where the
  // arc sits on the unit circle, then scale up the coordinates.

  const double angle_sweep = M_PI / 2.;  // minimize convex hull.
  const double angle_incr  = (angle_delta > 0) ? angle_sweep : -angle_sweep;

  while (angle_delta != 0.)
    {
      const double theta = angle;
      const double sweep = fabs(angle_delta) <= angle_sweep ? angle_delta : angle_incr;

      angle += sweep;
      angle_delta -= sweep;

      // Coordinates of the control point and the end point on the unit circle.
      const double half_sweep = sweep * 0.5;
      const double w          = cos(half_sweep);

      double control_x = cos(theta + half_sweep) / w;
      double control_y = sin(theta + half_sweep) / w;

      double end_x = cos(theta + sweep);
      double end_y = sin(theta + sweep);

      // Scale them to the ellipse's radii.
      control_x *= rx;
      control_y *= ry;
      end_x *= rx;
      end_y *= ry;

      // Rotate them + translate them.
      double c_x = cx + control_x * cos_phi - control_y * sin_phi;
      double c_y = cy + control_x * sin_phi + control_y * cos_phi;

      double n_x = cx + end_x * cos_phi - end_y * sin_phi;
      double n_y = cy + end_x * sin_phi + end_y * cos_phi;

      // The weight is the cosine of the half-sweep.
      addRatQuadTo(c_x, c_y, n_x, n_y, w);
    }
}

void
PathSink::addSvgArcTo(double x0,
                      double y0,
                      double x,
                      double y,
                      double rx,
                      double ry,
                      double x_axis_rotation_radians,
                      bool   large_arc_flag,
                      bool   sweep_flag)
{
  arc_center_parameters_t params = arc_center_parameters_from_endpoint(
    (const arc_endpoint_parameters_t){ x0,
                                       y0,
                                       x,
                                       y,
                                       large_arc_flag,
                                       sweep_flag,
                                       rx,
                                       ry,
                                       x_axis_rotation_radians });

  addArcTo(params.cx,
           params.cy,
           params.rx,
           params.ry,
           params.phi,
           params.theta,
           params.theta_delta);
}

// This is cos(PI/4), a.k.a. sqrt(2)/2, which happens to be the rational quad
// weight to use to render a quarter-circle arc.
static const double kCos45degrees = 0.7071067811865475244008;

// Add an ellipse path to a given builder. The ellipse is always axis-aligned.
// Note that this adds a full path (i.e. with begin() ... end() calls).
bool
PathSink::addEllipsePath(double center_x, double center_y, double radius_x, double radius_y)
{
  begin();

  // Implement the ellipse as four rational quadratic beziers. One per quadrant.
  // It is possible to use only 3 beziers but this results in a much wider
  // convex hull / bounding box.
  //
  // This always starts on (cx + rx, cy) in counter-clockwise orientation
  // (assuming x-rightwards and y-upwards axis).
  //
  // To get clockwise arcs, negate radius_y or radius_x, but not both.

  // Rational weight to turn a rational quad into a circle arc.
  const double rat_weight = kCos45degrees;

  // clang-format: off
  double coords[] = {
    // 0: start point
    center_x + radius_x,
    center_y,

    // 2: first quadrant / rational quad
    center_x + radius_x,
    center_y + radius_y,
    center_x,
    center_y + radius_y,
    rat_weight,

    // 7: second quadrant
    center_x - radius_x,
    center_y + radius_y,
    center_x - radius_x,
    center_y,
    rat_weight,

    // 12: third quadrant
    center_x - radius_x,
    center_y - radius_y,
    center_x,
    center_y - radius_y,
    rat_weight,

    // 17: last quadrant
    center_x + radius_x,
    center_y - radius_y,
    center_x + radius_x,
    center_y,
    rat_weight,
  };
  // clang-format: on

  addItem(MOVE_TO, coords + 0);
  addItem(RAT_QUAD_TO, coords + 2);
  addItem(RAT_QUAD_TO, coords + 7);
  addItem(RAT_QUAD_TO, coords + 12);
  addItem(RAT_QUAD_TO, coords + 17);

  return end();
}

bool
PathSink::addRectPath(double x, double y, double w, double h)
{
  begin();
  addMoveTo(x, y);
  addLineTo(x + w, y);
  addLineTo(x + w, y + h);
  addLineTo(x, y + h);
  addLineTo(x, y);
  return end();
}

bool
PathSink::addRoundedRectPath(double x, double y, double w, double h, double rx, double ry)
{
  if (rx == 0. || ry == 0.)
    return addRectPath(x, y, w, h);

  const double rat_weight = kCos45degrees;

  begin();

  addMoveTo(x + rx, y);
  addLineTo(x + w - rx, y);
  addRatQuadTo(x + w, y, x + w, y + ry, rat_weight);
  addLineTo(x + w, y + h - ry);
  addRatQuadTo(x + w, y + h, x + w - rx, y + h, rat_weight);
  addLineTo(x + rx, y + h);
  addRatQuadTo(x, y + h, x, y + h - ry, rat_weight);
  addLineTo(x, y + ry);
  addRatQuadTo(x, y, x + rx, y, rat_weight);

  return end();
}
