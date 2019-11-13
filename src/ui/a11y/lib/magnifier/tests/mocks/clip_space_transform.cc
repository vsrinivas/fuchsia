// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/tests/mocks/clip_space_transform.h"

namespace accessibility_test {

bool ClipSpaceTransform::operator==(const ClipSpaceTransform& other) const {
  return x == other.x && y == other.y && scale == other.scale;
}

glm::vec2 ClipSpaceTransform::Apply(const glm::vec2& pt) const {
  return {pt.x * scale + x, pt.y * scale + y};
}

std::ostream& operator<<(std::ostream& o, const ClipSpaceTransform& t) {
  return o << "* " << t.scale << " + (" << t.x << ", " << t.y << ")";
}

}  // namespace accessibility_test
