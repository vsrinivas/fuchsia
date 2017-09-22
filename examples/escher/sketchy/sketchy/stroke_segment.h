// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "sketchy/cubic_bezier.h"

namespace sketchy {

// A segment of a |Stroke|.  Encapsulates a cubic Bezier curve, as well as the
// length and an arc-length parameterization of that curve.
class StrokeSegment {
 public:
  StrokeSegment(CubicBezier2f curve);

  const CubicBezier2f& curve() const { return curve_; }
  const CubicBezier1f& arc_length_parameterization() const {
    return arc_length_parameterization_;
  }
  float length() const { return length_; }

  bool operator==(const StrokeSegment& other) const {
    // If the curves are identical, then there is no need to check the length
    // and arc-length parameterization.
    return curve_ == other.curve_;
  }

 private:
  const CubicBezier2f curve_;
  CubicBezier1f arc_length_parameterization_;
  float length_;
};

}  // namespace sketchy
