// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <utility>

#include "sketchy/types.h"

namespace sketchy {

template <typename VecT>
struct CubicBezier;
typedef CubicBezier<float> CubicBezier1f;
typedef CubicBezier<vec2> CubicBezier2f;

template <typename VecT>
struct CubicBezier {
  VecT pts[4];

  VecT Evaluate(float t) const;
  VecT Evaluate(float t, VecT* tmp3, VecT* tmp2) const;

  bool operator==(const CubicBezier<VecT>& other) const {
    return pts[0] == other.pts[0] && pts[1] == other.pts[1] &&
           pts[2] == other.pts[2] && pts[3] == other.pts[3];
  }

  // Split into two curves at the specified parameter.
  std::pair<CubicBezier<VecT>, CubicBezier<VecT>> Split(float t) const;

  // Compute the cumulative arc length of the curve.
  // TODO: some inputs trigger an infinite recursion; |debug_depth| is used
  // to detect these conditions.
  float ArcLength(uint8_t debug_depth = 0) const;

  // Compute an arc-length parameterization of this curve.  In other words,
  // the following code:
  //   CubicBezier2f bez = SomehowObtainBezierCurve(...);
  //   auto reparam = bez.ArcLengthParameterization();
  //   std::vector<vec2> points;
  //   for (float t = 0.f; t <= 1.f; ++t) {
  //     points.push_back(bez.Evaluate(reparam.Evaluate(t)));
  //   }
  // ... results in a collection of points that are approximately equally-spaced
  // along the curve.
  std::pair<CubicBezier1f, float> ArcLengthParameterization() const;
};

typedef CubicBezier<float> CubicBezier1f;
typedef CubicBezier<vec2> CubicBezier2f;

template <typename VecT>
CubicBezier<VecT> FitCubicBezier(const VecT* pts,
                                 int count,
                                 const float* params,
                                 float param_shift,
                                 float param_scale,
                                 VecT endpoint_tangent_0,
                                 VecT endpoint_tangent_1);

CubicBezier2f FitCubicBezier2f(const vec2* pts,
                               int count,
                               const float* params,
                               float param_shift,
                               float param_scale,
                               vec2 endpoint_tangent_0,
                               vec2 endpoint_tangent_1);

std::pair<vec2, vec2> EvaluatePointAndNormal(const CubicBezier<vec2>& bez,
                                             float t);

}  // namespace sketchy
