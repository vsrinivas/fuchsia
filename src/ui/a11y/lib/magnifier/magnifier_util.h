// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_MAGNIFIER_MAGNIFIER_UTIL_H_
#define SRC_UI_A11Y_LIB_MAGNIFIER_MAGNIFIER_UTIL_H_

#include <map>

#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

#include <glm/glm.hpp>

namespace a11y {

// Represents a change from the previous GestureContext state.
struct Delta {
  Delta& operator+=(const Delta& other);
  bool operator==(const Delta& other) const;

  // Delta translation is expressed in the coordinate system determined by the input source. For
  // most use cases, this is the view space, which is x-right y-down and scaled according to view
  // properties.
  glm::vec2 translation;

  float scale = 1;
};

// Converts a PointF to a vec2.
glm::vec2 ToVec2(::fuchsia::math::PointF point);

// Returns the Delta for two GestureContexts.
// This method expects that |current| and |previous| have the same set of
// pointers. If not, it will return the "NOOP" Delta with a translation of (0,
// 0) and a scale of 1.
Delta GetDelta(const GestureContext& current, const GestureContext& previous);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_MAGNIFIER_UTIL_H_
