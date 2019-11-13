// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_CLIP_SPACE_TRANSFORM_H_
#define SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_CLIP_SPACE_TRANSFORM_H_

#include <ostream>

#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace accessibility_test {

struct ClipSpaceTransform {
  static constexpr ClipSpaceTransform identity() { return {}; }

  float x = 0, y = 0, scale = 1;

  bool operator==(const ClipSpaceTransform& other) const;

  // Transforms an unmagnified NDCoordinate by this clip-space transform.
  glm::vec2 Apply(const glm::vec2& pt) const;

  // Convenience accessor for (x, y).
  glm::vec2 translation() const { return {x, y}; }
};

std::ostream& operator<<(std::ostream& o, const ClipSpaceTransform& t);

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_MOCKS_CLIP_SPACE_TRANSFORM_H_
