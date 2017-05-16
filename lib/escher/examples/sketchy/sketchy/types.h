// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"

// Safely include <glm/glm.hpp> despite Magenta countof() macro.
#include "escher/geometry/types.h"

namespace sketchy {

using glm::vec2;
using glm::vec3;
using glm::length;
using glm::normalize;
using glm::dot;

// Compute distance between two points.
template <typename VecT>
float distance(VecT a, VecT b) {
  return length(b - a);
}

}  // namespace sketchy
