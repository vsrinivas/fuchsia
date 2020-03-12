// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_PATH_SINK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_PATH_SINK_H_

#include <float.h>
#include <stdbool.h>
#include <stddef.h>

#include "tests/common/affine_transform.h"

// An abstract base class for objects that can be used to build vector path
// objects by adding individual moveto/lineto/quadto/etc items to it,
//
// Derived classes should override the begin(), addItem() and end() methods
// only, while callers may use the convenience methods like addMoveTo(),
// addLineTo(), addXXXTo(), addXXXPath() instead.
//
class PathSink {
 public:
  virtual ~PathSink() = default;

  enum ItemType
  {
    MOVE_TO = 0,
    LINE_TO,
    QUAD_TO,
    CUBIC_TO,
    RAT_QUAD_TO,
    RAT_CUBIC_TO,

    ItemTypeCount,  // Must be last, do not remove.
  };

  static constexpr char kArgsPerItemType[ItemTypeCount] = {
    2,  // MOVE_TO x y
    2,  // LINE_TO x y
    4,  // QUAD_TO cx cy x y
    6,  // CUBIC_TO c1x c1y c2x c2y x y
    5,  // RAT_QUAD_TO cx cy x y w
    8,  // RAT_CUBIC_TO c1x c1y c2x c2y x y w1 w2
  };

  static constexpr char kCoordPairsPerItemType[ItemTypeCount] = {
    1,  // MOVE_TO x y
    1,  // LINE_TO x y
    2,  // QUAD_TO cx cy x y
    3,  // CUBIC_TO c1x c1y c2x c2y x y
    2,  // RAT_QUAD_TO cx cy x y [w ignored]
    3,  // RAT_CUBIC_TO c1x c1y c2x c2y x y [w1 w2 ignored]
  };

  static constexpr size_t kMaxCoords = 8;

  // Begin a new path.
  virtual void
  begin() = 0;

  // Generic function to add a new path item. The only one to be overloaded
  // by implementations. Callers can use the helper functions below
  // instead.
  virtual void
  addItem(ItemType item_type, const double * coords) = 0;

  void
  addMoveTo(double x, double y)
  {
    double coords[2] = { x, y };
    addItem(MOVE_TO, coords);
  }

  void
  addLineTo(double x, double y)
  {
    double coords[2] = { x, y };
    addItem(LINE_TO, coords);
  }

  void
  addQuadTo(double cx, double cy, double x, double y)
  {
    double coords[4] = { cx, cy, x, y };
    addItem(QUAD_TO, coords);
  }

  void
  addCubicTo(double c1x, double c1y, double c2x, double c2y, double x, double y)
  {
    double coords[6] = { c1x, c1y, c2x, c2y, x, y };
    addItem(CUBIC_TO, coords);
  }

  void
  addRatQuadTo(double cx, double cy, double x, double y, double w)
  {
    double coords[5] = { cx, cy, x, y, w };
    addItem(RAT_QUAD_TO, coords);
  }

  void
  addRatCubicTo(
    double c1x, double c1y, double c2x, double c2y, double x, double y, double w1, double w2)
  {
    double coords[8] = { c1x, c1y, c2x, c2y, x, y, w1, w2 };
    addItem(RAT_CUBIC_TO, coords);
  }

  // Add rational quadratics that match an elliptical arc segment to the
  // current path.
  //
  // |cx, cy| is the ellipse's center.
  // |rx, ry| are the ellipse's radii.
  // |x_axis_rotation| is the ellipse's rotation of the x-axis in radians.
  // |angle_start| and |angle_delta| define the start angle and the sweep
  // to perform.
  //
  void
  addArcTo(double cx,
           double cy,
           double rx,
           double ry,
           double x_axis_rotation,
           double angle_start,
           double angle_delta);

  // Alternative way to add elliptical arcs to the current path, using
  // SVG-specific parameters. |x0, y0| must be the current/last point
  // added to the path. |x, y| is the final arc point, |rx, ry| are the
  // ellipse's radii, |x_axis_rotation| is the ellipse's rotation in radians,
  // and |large_arc_flag| and |sweep_flag| control the arc's selection.
  void
  addSvgArcTo(double x1,
              double y1,
              double x2,
              double y2,
              double rx,
              double ry,
              double x_axis_rotation,
              bool   large_arc_flag,
              bool   sweep_flag);

  // Finish the path. Returns true on success, or false on failure,
  // which can be used to notify the caller that a fatal error happened.
  bool virtual end() = 0;

  // Add an ellipse path to a given builder. The ellipse is always axis-aligned.
  // Note that this adds a full path (i.e. with begin() ... end() calls).
  bool
  addEllipsePath(double center_x, double center_y, double radius_x, double radius_y);

  // Adds a rectangle path to a given builder.
  // |x,y,w,h| are the rectangle's origin and dimensions.
  bool
  addRectPath(double x, double y, double w, double h);

  // Add a rounded rectangle path to a given builder.
  // |x,y,w,h| are the rectangle's origin and dimensions.
  // |rx,ry| are the rounded corner radiuses in horizontal and vertical
  // dimensions (e.g. the top-left corner is (x,y+ry) -> (x,y) -> (x+rx,y)).
  bool
  addRoundedRectPath(double x, double y, double w, double h, double rx, double ry);
};

//
//  Base path builder implementations.
//

// An AffinePathSink applies an affine_transform_t to all coordinates
// it receives then send them to a target PathSink instance.
//
// Usage is:
//     1) Create instance, passing the transform by value, and a reference
//        to the target.
//
//     2) Build a path with it as usual.
//
class AffinePathSink : public PathSink {
 public:
  AffinePathSink(const affine_transform_t * transform, PathSink * target) : target_(*target)
  {
    resetTransform(transform);
  }

  void
  resetTransform(const affine_transform_t * transform)
  {
    transform_ = transform ? *transform : affine_transform_identity;
  }

  void
  begin() override
  {
    target_.begin();
  }

  void
  addItem(ItemType item_type, const double * coords) override
  {
    double new_coords[kMaxCoords];

    for (int nn = 0; nn < kArgsPerItemType[item_type]; ++nn)
      new_coords[nn] = coords[nn];

    // Transform only the needed coordinates pairs.
    int num_pairs = kCoordPairsPerItemType[item_type];
    for (int nn = 0; nn < num_pairs; ++nn)
      affine_transform_apply_xy(&transform_, &new_coords[nn * 2]);

    target_.addItem(item_type, new_coords);
  }

  bool
  end() override
  {
    return target_.end();
  }

 protected:
  affine_transform_t transform_;
  PathSink &         target_;
};

// A PathSink derived class that computes the bounding box of all path
// points. Usage is:
//
//    1) Create instance.
//    2) Send path items to it as usual.
//    3) Retrieve bounds with xmin(), xmax(), ymin(), and ymax() methods.
//
// NOTE: If not path points were recorded, then
//       |xmin() > xmax() && ymin() > ymax()| will be true.
//
class BoundingPathSink : public PathSink {
 public:
  BoundingPathSink() = default;

  struct Bounds
  {
    double xmin = DBL_MAX;
    double ymin = DBL_MAX;
    double xmax = -DBL_MAX;
    double ymax = -DBL_MAX;

    bool
    valid() const
    {
      return xmin <= ymin && xmax <= ymax;
    }
  };

  const Bounds &
  bounds() const
  {
    return b_;
  }

  // Method overrides.

  void
  begin() override
  {
  }

  bool
  end() override
  {
    return true;
  }

  void
  addItem(PathSink::ItemType item_type, const double * coords) override
  {
    for (int nn = 0; nn < kCoordPairsPerItemType[item_type]; ++nn)
      {
        double x = coords[nn * 2];
        double y = coords[nn * 2 + 1];

        if (x < b_.xmin)
          b_.xmin = x;
        if (x > b_.xmax)
          b_.xmax = x;
        if (y < b_.ymin)
          b_.ymin = y;
        if (y > b_.ymax)
          b_.ymax = y;
      }
  }

 private:
  Bounds b_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_PATH_SINK_H_
