// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/mozart/src/scene/resources/resource.h"
#include "lib/escher/escher/geometry/types.h"

namespace mozart {
namespace scene {

struct HitTestResult {
  /// The tag node ID that passed the hit test.
  ResourceId node = 0;

  /// The point in the coordinate space of the tag node where the hit test
  /// occurred.
  escher::vec2 point;
};

using HitTestResults = std::vector<HitTestResult>;

}  // namespace scene
}  // namespace mozart
