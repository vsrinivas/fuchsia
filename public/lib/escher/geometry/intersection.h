// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/geometry/bounding_box.h"
#include "lib/escher/geometry/types.h"

namespace escher {

// Returns whether a ray intersects an axis-aligned bounding box. Upon return,
// |out_distance| contains the distance from the ray origin to the intersection
// point in units of ray length.
bool IntersectRayBox(const escher::ray4& ray, const escher::BoundingBox& box,
                     float* out_distance);

}  // namespace escher