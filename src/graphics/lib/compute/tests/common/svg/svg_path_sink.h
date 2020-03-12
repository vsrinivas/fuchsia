// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_PATH_SINK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_PATH_SINK_H_

#include <memory>

#include "tests/common/path_sink.h"

// A simple object that implements the SVG path building operations
// and feeds them into a target PathSink.

class SvgPathSink {
 public:
  // Constructor. Takes a target PathSink instance, and an optional
  // initial transform that will be applied to all coordinates being
  // sent to the target.
  explicit SvgPathSink(PathSink * target, const affine_transform_t * transform = nullptr);

  ~SvgPathSink();

  void
  resetTransform(const affine_transform_t * transform);

  // Basic shapes.
  bool
  circle(double cx, double cy, double r);
  bool
  ellipse(double cx, double cy, double rx, double ry);
  bool
  line(double x1, double y1, double x2, double y2);
  bool
  rect(double x, double y, double w, double h);

  // Polygon and Polyline.
  void
  polyStart(bool closed);  // followed by one or more polyPoint() + one final polyEnd().
  void
  polyPoint(double x, double y, bool relative = false);
  bool
  polyEnd();

  // Path data.
  void
  pathBegin(bool closed = true);

  void
  moveTo(double x, double y, bool relative = false);

  void
  lineTo(double x, double y, bool relative = false);

  void
  hlineTo(double x, bool relative = false);
  void
  vlineTo(double x, bool relative = false);

  void
  quadTo(double cx, double cy, double x, double y, bool relative = false);

  void
  cubicTo(
    double c1x, double c1y, double c2x, double c2y, double x, double y, bool relative = false);

  void
  smoothQuadTo(double x, double y, bool relative = false);

  void
  smoothCubicTo(double c2x, double c2y, double x, double y, bool relative = false);

  void
  ratQuadTo(double cx, double cy, double x, double y, double w, bool relative = false);

  void
  ratCubicTo(double c1x,
             double c1y,
             double c2x,
             double c2y,
             double x,
             double y,
             double w1,
             double w2,
             bool   relative = false);

  void
  arcTo(double x,
        double y,
        double rx,
        double ry,
        double x_axis_rotation_radians,
        bool   large_arc_flag,
        bool   sweep_flag,
        bool   relative = false);

  void
  pathClose();

  bool
  pathEnd();

 protected:
  void
  setLast(double x, double y)
  {
    setLast(x, y, x, y);
  };
  void
  setLast(double x, double y, double xp, double yp);

  PathSink *     target_  = nullptr;
  PathSink *     target0_ = nullptr;
  AffinePathSink affine_sink_;

  // (x0,y0) is the first point in the current contour (i.e. last moveto)
  // (x,y) is the last point.
  // (xp,yp) is the previous last point if any, or (x,y) otherwise.
  double x0_ = 0., y0_ = 0., x_ = 0., y_ = 0., xp_ = 0., yp_ = 0.;
  bool   path_closed_ = false;
  bool   poly_start_  = false;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_PATH_SINK_H_
