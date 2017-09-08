// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/shapes/circle_shape.h"

namespace scene_manager {

const ResourceTypeInfo CircleShape::kTypeInfo = {
    ResourceType::kShape | ResourceType::kCircle, "CircleShape"};

CircleShape::CircleShape(Session* session,
                         scenic::ResourceId id,
                         float initial_radius)
    : PlanarShape(session, id, CircleShape::kTypeInfo),
      radius_(initial_radius) {}

bool CircleShape::ContainsPoint(const escher::vec2& point) const {
  return point.x * point.x + point.y * point.y <= radius_ * radius_;
}

escher::Object CircleShape::GenerateRenderObject(
    const escher::mat4& transform,
    const escher::MaterialPtr& material) {
  return escher::Object::NewCircle(transform, radius_, material);
}

}  // namespace scene_manager
