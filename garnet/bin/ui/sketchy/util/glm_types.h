// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_UTIL_GLM_TYPES_H_
#define GARNET_BIN_UI_SKETCHY_UTIL_GLM_TYPES_H_

#include "src/ui/lib/escher/forward_declarations.h"

// Safely include <glm/glm.hpp> despite Zircon countof() macro.
#include "src/ui/lib/escher/geometry/types.h"

namespace sketchy_service {

using glm::dot;
using glm::length;
using glm::normalize;
using glm::vec2;
using glm::vec3;

// Compute distance between two points.
template <typename VecT>
float distance(VecT a, VecT b) {
  return length(b - a);
}

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_UTIL_GLM_TYPES_H_
