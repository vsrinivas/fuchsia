// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/svg/svg_path_sink.h"

SvgPathSink::SvgPathSink(PathSink * target, const affine_transform_t * transform)
    : target_(target), target0_(target), affine_sink_(transform, target)
{
  resetTransform(transform);
}

SvgPathSink::~SvgPathSink() = default;

void
SvgPathSink::resetTransform(const affine_transform_t * transform)
{
  if (transform)
    {
      affine_sink_.resetTransform(transform);
      target_ = &affine_sink_;
    }
  else
    {
      target_ = target0_;
    }
}

bool
SvgPathSink::circle(double cx, double cy, double r)
{
  return ellipse(cx, cy, r, r);
}

bool
SvgPathSink::ellipse(double cx, double cy, double rx, double ry)
{
  return target_->addEllipsePath(cx, cy, rx, ry);
}

bool
SvgPathSink::line(double x1, double y1, double x2, double y2)
{
  pathBegin();
  moveTo(x1, y1);
  lineTo(x2, y2);
  return pathEnd();
}

bool
SvgPathSink::rect(double x, double y, double w, double h)
{
  pathBegin();
  moveTo(x, y);
  lineTo(x + w, y);
  lineTo(x + w, y + h);
  lineTo(x, y + h);
  lineTo(x, y);
  return pathEnd();
}

void
SvgPathSink::polyStart(bool closed)
{
  pathBegin(closed);
  poly_start_ = false;
}

void
SvgPathSink::polyPoint(double x, double y, bool relative)
{
  if (!poly_start_)
    {
      moveTo(x, y, relative);
      poly_start_ = true;
    }
  else
    {
      lineTo(x, y, relative);
    }
}

bool
SvgPathSink::polyEnd()
{
  return pathEnd();
}

void
SvgPathSink::moveTo(double x, double y, bool relative)
{
  if (relative)
    {
      x += x_;
      y += y_;
    }

  pathClose();

  x_ = x0_ = xp_ = x;
  y_ = y0_ = yp_ = y;

  target_->addMoveTo(x, y);
}

void
SvgPathSink::lineTo(double x, double y, bool relative)
{
  if (relative)
    {
      x += x_;
      y += y_;
    }

  target_->addLineTo(x, y);

  setLast(x, y);
}

void
SvgPathSink::hlineTo(double x, bool relative)
{
  if (relative)
    {
      x += x_;
    }
  lineTo(x, y_);
}

void
SvgPathSink::vlineTo(double y, bool relative)
{
  if (relative)
    {
      y += y_;
    }
  lineTo(x_, y);
}

void
SvgPathSink::quadTo(double cx, double cy, double x, double y, bool relative)
{
  if (relative)
    {
      cx += x_;
      cy += y_;
      x += x_;
      y += y_;
    }

  target_->addQuadTo(cx, cy, x, y);

  setLast(x, y, cx, cy);
}

void
SvgPathSink::smoothQuadTo(double x, double y, bool relative)
{
  if (relative)
    {
      x += x_;
      y += y_;
    }

  double cx = 2. * x_ - xp_;
  double cy = 2. * y_ - yp_;

  quadTo(cx, cy, x, y, false);
}

void
SvgPathSink::cubicTo(
  double c1x, double c1y, double c2x, double c2y, double x, double y, bool relative)
{
  if (relative)
    {
      c1x += x_;
      c1y += y_;
      c2x += x_;
      c2y += y_;
      x += x_;
      y += y_;
    }

  target_->addCubicTo(c1x, c1y, c2x, c2y, x, y);

  setLast(x, y, c2x, c2y);
}

void
SvgPathSink::smoothCubicTo(double c2x, double c2y, double x, double y, bool relative)
{
  if (relative)
    {
      c2x += x_;
      c2y += y_;
      x += x_;
      y += y_;
    }

  double c1x = 2. * x_ - xp_;
  double c1y = 2. * y_ - yp_;

  cubicTo(c1x, c1y, c2x, c2y, x, y, false);
}

void
SvgPathSink::ratQuadTo(double cx, double cy, double x, double y, double w, bool relative)
{
  if (relative)
    {
      cx += x_;
      cy += y_;
      x += x_;
      y += y_;
    }

  target_->addRatQuadTo(cx, cy, x, y, w);

  setLast(x, y, cx, cy);
}

void
SvgPathSink::ratCubicTo(double c1x,
                        double c1y,
                        double c2x,
                        double c2y,
                        double x,
                        double y,
                        double w1,
                        double w2,
                        bool   relative)
{
  if (relative)
    {
      c1x += x_;
      c1y += y_;
      c2x += x_;
      c2y += y_;
      x += x_;
      y += y_;
    }

  target_->addRatCubicTo(c1x, c1y, c2x, c2y, x, y, w1, w2);

  setLast(x, y, c2x, c2y);
}

void
SvgPathSink::arcTo(double x,
                   double y,
                   double rx,
                   double ry,
                   double x_axis_rotation_radians,
                   bool   large_arc_flag,
                   bool   sweep_flag,
                   bool   relative)
{
  if (relative)
    {
      x += x_;
      y += y_;
    }

  target_->addSvgArcTo(x_, y_, x, y, rx, ry, x_axis_rotation_radians, large_arc_flag, sweep_flag);

  setLast(x, y);
}

void
SvgPathSink::pathBegin(bool closed)
{
  x0_ = y0_ = x_ = y_ = 0.;
  path_closed_        = closed;
  target_->begin();
}

void
SvgPathSink::pathClose()
{
  if (path_closed_ && (x_ != x0_ || y_ != y0_))
    {
      lineTo(x0_, y0_);
    }
}

bool
SvgPathSink::pathEnd()
{
  pathClose();
  bool result = target_->end();

  path_closed_ = false;
  return result;
}

void
SvgPathSink::setLast(double x, double y, double xp, double yp)
{
  x_  = x;
  y_  = y;
  xp_ = xp;
  yp_ = yp;
}
