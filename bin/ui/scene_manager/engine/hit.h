// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/geometry/types.h"

namespace scene_manager {

// Describes where a hit occurred within the content of a tagged node.
struct Hit {
  // The node's tag value.
  uint32_t tag_value;

  // The inverse transformation matrix which maps the coordinate system of
  // the node at which the hit test was initiated into the local coordinate
  // system of the node which was hit.  To convert the hit test ray into the
  // node's local coordinate system, multiply |inverse_transform| by the ray.
  escher::mat4 inverse_transform;

  // The distance from the ray's origin to the closest point of intersection
  // in multiples of the ray's direction vector.  To compute the point of
  // intersection, multiply the ray's direction vector by |distance| and
  // add the ray's origin point.
  float distance;
};

}  // namespace scene_manager
