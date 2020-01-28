// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/shapes/circle_shape.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo CircleShape::kTypeInfo = {ResourceType::kShape | ResourceType::kCircle,
                                                 "CircleShape"};

CircleShape::CircleShape(Session* session, SessionId session_id, ResourceId id,
                         float initial_radius)
    : PlanarShape(session, session_id, id, CircleShape::kTypeInfo), radius_(initial_radius) {}

bool CircleShape::ContainsPoint(const escher::vec2& point) const {
  return point.x * point.x + point.y * point.y <= radius_ * radius_;
}


}  // namespace gfx
}  // namespace scenic_impl
