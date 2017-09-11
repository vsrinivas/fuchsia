// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sketchy/cubic_bezier.h"

#include "lib/fxl/logging.h"

namespace sketchy {

template <typename VecT>
VecT CubicBezier<VecT>::Evaluate(float t) const {
  VecT tmp3[3];
  VecT tmp2[2];
  return Evaluate(t, tmp3, tmp2);
}

// TODO: inline makes performance difference?
template <typename VecT>
VecT CubicBezier<VecT>::Evaluate(float t, VecT* tmp3, VecT* tmp2) const {
  float one_minus_t = 1.0 - t;
  tmp3[0] = pts[0] * one_minus_t + pts[1] * t;
  tmp3[1] = pts[1] * one_minus_t + pts[2] * t;
  tmp3[2] = pts[2] * one_minus_t + pts[3] * t;
  tmp2[0] = tmp3[0] * one_minus_t + tmp3[1] * t;
  tmp2[1] = tmp3[1] * one_minus_t + tmp3[2] * t;
  return tmp2[0] * one_minus_t + tmp2[1] * t;
}

// TODO: test
// auto split = bez.split;
// for (t = 0.0; t <= 1.0; ++t) {
//   EXPECT_EQ(split.first.eval(t), bez.eval(t/2));
//   EXPECT_EQ(split.second.eval(t), bez.eval(t/2 + 0.5));
// }
template <typename VecT>
std::pair<CubicBezier<VecT>, CubicBezier<VecT>> CubicBezier<VecT>::Split(
    float t) const {
  VecT tmp3[3];
  VecT tmp2[2];
  VecT split_pt = Evaluate(t, tmp3, tmp2);

  std::pair<CubicBezier<VecT>, CubicBezier<VecT>> result;
  result.first.pts[0] = pts[0];
  result.first.pts[1] = tmp3[0];
  result.first.pts[2] = tmp2[0];
  result.first.pts[3] = split_pt;
  result.second.pts[0] = split_pt;
  result.second.pts[1] = tmp2[1];
  result.second.pts[2] = tmp3[2];
  result.second.pts[3] = pts[3];
  return result;
}

// Compute the cumulative arc length of the curve, using the approach
// described in "Adaptive subdivision and the length of Bezier curves" by Jens
// Gravsen. The insight is that the length is bounded below by the length of
// the line segment (pt0, pt3), and bounded above by the sum of the line
// segments (pt0, pt1), (pt1, pt2), (pt2, pt3).
template <typename VecT>
float CubicBezier<VecT>::ArcLength(uint8_t debug_depth) const {
  constexpr float kMaxErrorRate = 0.01f;
  constexpr float kEpsilon = 0.000005f;
  FXL_DCHECK(debug_depth < 100);

  float upper_bound = distance(pts[0], pts[1]) + distance(pts[1], pts[2]) +
                      distance(pts[2], pts[3]);
  float lower_bound = distance(pts[0], pts[3]);

  if (!(upper_bound > 0.f)) {
    // Catch zero and NaN before they become a problem.
    return 0.f;
  } else if ((upper_bound - lower_bound) / upper_bound <= kMaxErrorRate * 2.f) {
    // Returning the mean of the two bounds guarantees that the result is within
    // the error tolerance.
    return 0.5 * (upper_bound + lower_bound);
  } else if (upper_bound < kEpsilon) {
    // In some cases, floating point precision will result in non-convergence;
    // when this is detected, we explicitly terminate recursion.
    // TODO: Is a constant threshold the right approach?  Is this the right
    // threshold ?
    return 0.5 * (upper_bound + lower_bound);
  } else {
    // This curve is not flat enough.  Split into two curves, and recursively
    // evaluate the length of each one.
    auto pair = Split(0.5);
    float length_1 = pair.first.ArcLength(debug_depth + 1);
    float length_2 = pair.second.ArcLength(debug_depth + 1);
    return length_1 + length_2;
  }
}

// Uses a simplified version of "Approximate Arc Length Parameterization" by
// Walter and Fournier.  In particular, this version does not detect cases where
// it would be desirable to split the reparameterization curve into two
// segments.
template <typename VecT>
std::pair<CubicBezier1f, float> CubicBezier<VecT>::ArcLengthParameterization()
    const {
  const float full_length = ArcLength();
  const float one_third_length = Split(1.f / 3.f).first.ArcLength();
  const float two_thirds_length = Split(2.f / 3.f).first.ArcLength();
  const float normalizer = 1.f / full_length;
  const float s0 = one_third_length * normalizer;
  const float s1 = two_thirds_length * normalizer;

  CubicBezier1f bez;
  bez.pts[0] = 0.f;
  bez.pts[1] = (18.f * s0 - 9.f * s1 + 2.0) / 6.0;
  bez.pts[2] = (-9.f * s0 + 18.f * s1 - 5.0) / 6.0;
  bez.pts[3] = 1.f;

  return std::make_pair(bez, full_length);
}

template <typename VecT>
CubicBezier<VecT> FitCubicBezier(const VecT* pts,
                                 int count,
                                 const float* params,
                                 float param_shift,
                                 float param_scale,
                                 VecT endpoint_tangent_0,
                                 VecT endpoint_tangent_1) {
  float c00, c01, c10, c11;
  c00 = c01 = c10 = c11 = 0.f;
  VecT x;

  for (int i = 0; i < count; ++i) {
    float t = (params[i] + param_shift) * param_scale;
    float omt = 1.f - t;
    float b0 = omt * omt * omt;
    float b1 = 3.f * t * omt * omt;
    float b2 = 3.f * t * t * omt;
    float b3 = t * t * t;
    VecT a0 = endpoint_tangent_0 * b1;
    VecT a1 = endpoint_tangent_1 * b2;
    c00 += dot(a0, a0);
    c01 += dot(a0, a1);
    // c10 == dot(a1, a0) == c01, so don't compute here,
    // but instead set it just after the loop.
    c11 += dot(a1, a1);
    VecT tmp = pts[i] - (pts[0] * (b0 + b1) + pts[count - 1] * (b2 + b3));
    x[0] += dot(a0, tmp);
    x[1] += dot(a1, tmp);
  }

  c10 = c01;

  float det_c0_c1 = c00 * c11 - c10 * c01;
  float det_c0_x = c00 * x[1] - c01 * x[0];
  float det_x_c1 = x[0] * c11 - x[1] * c01;

  if (det_c0_c1 == 0.f)
    det_c0_c1 = c00 * c11 * 10e-12;

  // Compute alpha values used to determine the distance along the left/right
  // tangent vectors to place the middle two control points.  If either alpha
  // value is negative, recompute it using Wu/Barsky heuristic.
  float alpha_l = det_x_c1 / det_c0_c1;
  float alpha_r = det_c0_x / det_c0_c1;
  if (alpha_l < 0.0 || alpha_r < 0.0) {
    // Alpha was negative, so use Wu/Barsky heuristic to place points.
    // TODO: if only one alpha value is negative, should only that one be
    // adjusted?
    alpha_l = alpha_r = distance(pts[0], pts[count - 1]);
  }

  // Set all 4 control points and return the curve.
  CubicBezier<VecT> fit;
  fit.pts[0] = pts[0];
  fit.pts[1] = pts[0] + endpoint_tangent_0 * alpha_l;
  fit.pts[2] = pts[count - 1] + endpoint_tangent_1 * alpha_r;
  fit.pts[3] = pts[count - 1];
  return fit;
}

CubicBezier2f FitCubicBezier2f(const vec2* pts,
                               int count,
                               const float* params,
                               float param_shift,
                               float param_scale,
                               vec2 endpoint_tangent_0,
                               vec2 endpoint_tangent_1) {
// TODO: consider using vectorized version
#if 1
  return FitCubicBezier<vec2>(pts, count, params, param_shift, param_scale,
                              endpoint_tangent_0, endpoint_tangent_1);
#else
  float c00, c01, c10, c11;
  c00 = c01 = c10 = c11 = 0.0f;
  vec2 x;

#if 1
  // Non-vectorized version
  for (int i = 0; i < count; ++i) {
    float t = (params[i] + param_shift) * param_scale;
    float omt = 1.0 - t;
    float b0 = omt * omt * omt;
    float b1 = 3.0 * t * omt * omt;
    float b2 = 3.0 * t * t * omt;
    float b3 = t * t * t;
    vec2 a0 = endpoint_tangent_0 * b1;
    vec2 a1 = endpoint_tangent_1 * b2;
    c00 += dot(a0, a0);
    c01 += dot(a0, a1);
    // c10 == dot(a1, a0) == c01, so don't compute here,
    // but instead set it just after the loop.
    c11 += dot(a1, a1);
    vec2 tmp = pts[i] - (pts[0] * (b0 + b1) + pts[count - 1] * (b2 + b3));
    x[0] += dot(a0, tmp);
    x[1] += dot(a1, tmp);
  }
#else  // VECTORIZE

  // Requires -ffast-math.  TODO: experiment with fp_contract pragma (maybe
  // globally enabling -ffast-math isn't necessary).

  float first_valx = pts[0].x;
  float first_valy = pts[0].y;
  float last_valx = pts[count - 1].x;
  float last_valy = pts[count - 1].y;

  float et0x = endpoint_tangent_0[0];
  float et0y = endpoint_tangent_0[1];
  float et1x = endpoint_tangent_1[0];
  float et1y = endpoint_tangent_1[1];

  float c00[2000];
  float c01[2000];
  float c11[2000];
  float x0[2000];
  float x1[2000];

#pragma clang loop vectorize(enable) interleave(enable)
  for (int i = 0; i < count; i++) {
    float t = (params[i] + param_shift) * param_scale;
    float omt = 1.0 - t;
    float b0 = omt * omt * omt;
    float b1 = 3.0 * t * omt * omt;
    float b2 = 3.0 * t * t * omt;
    float b3 = t * t * t;
    float b0b1 = b0 + b1;
    float b2b3 = b2 + b3;

    float a0x = et0x * b1;
    float a0y = et0y * b1;
    float a1x = et1x * b2;
    float a1y = et1y * b2;

    c00[i] = a0x * a0x + a0y * a0y;  // dot(a0, a0);
    c01[i] = a0x * a1x + a0y * a1y;  // dot(a0, a1);
    c11[i] = a1x * a1x + a1y * a1y;  // dot(a1, a1);
    float tmpx = pts[i * 2] - first_valx * b0b1 - last_valx * b2b3;
    float tmpy = pts[i * 2 + 1] - first_valy * b0b1 - last_valy * b2b3;
    x0[i] = a0x * tmpx + a0y * tmpy;  // dot(a0, tmp);
    x1[i] = a1x * tmpx + a1y * tmpy;  // dot(a1, tmp);
  }

// Accumulate sums.  If we try to do this in the loop above, the vectorizer
// fails.
#pragma clang loop vectorize(enable) interleave(enable)
  for (int i = 0; i < count; i++) {
    c00 += c00[i];
    c01 += c01[i];
    c11 += c11[i];
    x[0] += x0[i];
    x[1] += x1[i];
  }
#endif  // VECTORIZE

  c10 = c01;

  float det_c0_c1 = c00 * c11 - c10 * c01;
  float det_c0_x = c00 * x[1] - c01 * x[0];
  float det_x_c1 = x[0] * c11 - x[1] * c01;

  if (det_c0_c1 == 0.0)
    det_c0_c1 = c00 * c11 * 10e-12;

  // Compute alpha values used to determine the distance along the left/right
  // tangent vectors to place the middle two control points.  If either alpha
  // value is negative, recompute it using Wu/Barsky heuristic.
  float alpha_l = det_x_c1 / det_c0_c1;
  float alpha_r = det_c0_x / det_c0_c1;
  if (alpha_l < 0.0 || alpha_r < 0.0) {
    // Alpha was negative, so use Wu/Barsky heuristic to place points.
    // TODO: if only one alpha value is negative, should only that one be
    // adjusted?
    alpha_l = alpha_r = distance(pts[0], pts[count - 1]);
  }

  // Set all 4 control points and return the curve.
  CubicBezier2f fit;
  fit.pts[0] = pts[0];
  fit.pts[1] = pts[0] + endpoint_tangent_0 * alpha_l;
  fit.pts[2] = pts[count - 1] + endpoint_tangent_1 * alpha_r;
  fit.pts[3] = pts[count - 1];
  return fit;
#endif
}

std::pair<vec2, vec2> EvaluatePointAndNormal(const CubicBezier<vec2>& bez,
                                             float t) {
  vec2 tmp3[3];
  vec2 tmp2[2];
  vec2 pt = bez.Evaluate(t, tmp3, tmp2);
  vec2 tangent = normalize(tmp2[1] - tmp2[0]);

  // Rotate tangent clockwise by 90 degrees before returning.
  return std::make_pair(pt, vec2{-tangent.y, tangent.x});
}

// Force instantiation.
template struct CubicBezier<float>;
template struct CubicBezier<vec2>;

}  // namespace sketchy
