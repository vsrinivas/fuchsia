// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/magnifier_util.h"

#include <lib/syslog/cpp/macros.h>

namespace a11y {

Delta& Delta::operator+=(const Delta& other) {
  translation += other.translation;
  scale *= other.scale;
  return *this;
}

bool Delta::operator==(const Delta& other) const {
  return translation == other.translation && scale == other.scale;
}

glm::vec2 ToVec2(::fuchsia::math::PointF point) { return glm::vec2(point.x, point.y); }

Delta GetDelta(const GestureContext& current, const GestureContext& previous) {
  Delta delta;

  // We only ever compute deltas after a gesture has been recognized and before
  // it's considered "complete". For every magnifier gesture, the same set of
  // fingers will be onscreen during this time. We should never encounter the
  // case in which |previous| and |current| have different sets of pointers.
  // We will check that each pointer ID in |current| is represented in
  // |previous| in the loop below, but for now, we can check that |current| and
  // |previous| have the same number of pointers. If we do encounter this case,
  // we will just return the "NOOP" Delta with a translation of (0, 0) and a
  // scale of 1.
  if (current.current_pointer_locations.size() != previous.current_pointer_locations.size()) {
    return delta;
  }

  auto previous_centroid = ToVec2(previous.CurrentCentroid(false /* use local coordinates */));
  auto current_centroid = ToVec2(current.CurrentCentroid(false /* use local coordinates */));

  delta.translation = current_centroid - previous_centroid;

  // We compute delta scale as the arithmetic mean of the scale of change in
  // relative distance between each pointer and the gesture centroid.
  float scale_sum = 0;

  for (const auto& entry : current.current_pointer_locations) {
    auto pointer_id = entry.first;

    // As above, if the set of pointers are different between the current and
    // previous contexts, return the "NOOP" Delta.
    if (!previous.current_pointer_locations.count(pointer_id)) {
      return Delta();
    }

    auto current_point = ToVec2(entry.second.ndc_point);
    auto previous_point = ToVec2(previous.current_pointer_locations.at(pointer_id).ndc_point);

    auto current_distance = glm::length(current_point - current_centroid);
    auto previous_distance = glm::length(previous_point - previous_centroid);

    scale_sum += current_distance / previous_distance;
  }

  scale_sum /= static_cast<float>(current.current_pointer_locations.size());
  delta.scale = scale_sum;

  return delta;
}

}  // namespace a11y
