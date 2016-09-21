// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/graph/transform_pair.h"

namespace compositor {

TransformPair::TransformPair(const SkMatrix44& forward)
    : forward_(forward),
      cached_inverse_(SkMatrix44::kUninitialized_Constructor) {}

TransformPair::~TransformPair() {}

const SkMatrix44& TransformPair::GetInverse() const {
  if (!cached_inverse_valid_) {
    if (!forward_.invert(&cached_inverse_)) {
      // Matrix is singular!
      // Return [0,0,0,0][0,0,0,0][0,0,0,0][0,0,0,1] (all zeroes except
      // last component).  This causes all points to be mapped to the origin
      // when transformed.
      cached_inverse_.setScale(0.f, 0.f, 0.f);
    }
    cached_inverse_valid_ = true;
  }
  return cached_inverse_;
}

const SkPoint TransformPair::InverseMapPoint(const SkPoint& point) const {
  SkScalar vec[4] = {point.x(), point.y(), 0.f, 1.f};
  GetInverse().mapScalars(vec);
  return SkPoint::Make(vec[0], vec[1]);
}

}  // namespace compositor
