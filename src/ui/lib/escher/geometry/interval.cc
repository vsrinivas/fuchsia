// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/interval.h"

#include <array>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/lib/escher/geometry/plane_ops.h"

namespace escher {

Interval::Interval(float min, float max) : min_(min), max_(max) {}

Interval Interval::Join(const Interval& interval) {
  if (is_empty()) {
    return interval;
  } else if (interval.is_empty()) {
    return *this;
  }

  float min = glm::min(min_, interval.min_);
  float max = glm::max(max_, interval.max_);

  return Interval(min, max);
}

Interval Interval::Intersect(const Interval& interval) const {
  if (is_empty()) {
    return *this;
  } else if (interval.is_empty()) {
    return interval;
  }

  float min = glm::max(min_, interval.min_);
  float max = glm::min(max_, interval.max_);
  if (max < min) {
    return Interval();
  }
  return Interval(min, max);
}

}  // namespace escher
