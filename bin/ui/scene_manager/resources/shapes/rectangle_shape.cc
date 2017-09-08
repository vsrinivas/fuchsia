// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/shapes/rectangle_shape.h"

namespace scene_manager {

const ResourceTypeInfo RectangleShape::kTypeInfo = {
    ResourceType::kShape | ResourceType::kRectangle, "RectangleShape"};

RectangleShape::RectangleShape(Session* session,
                               scenic::ResourceId id,
                               float initial_width,
                               float initial_height)
    : PlanarShape(session, id, RectangleShape::kTypeInfo),
      width_(initial_width),
      height_(initial_height) {}

bool RectangleShape::ContainsPoint(const escher::vec2& point) const {
  const escher::vec2 pt = point + escher::vec2(0.5f * width_, 0.5f * height_);
  return pt.x >= 0.f && pt.y >= 0.f && pt.x <= width_ && pt.y <= height_;
}

escher::Object RectangleShape::GenerateRenderObject(
    const escher::mat4& transform,
    const escher::MaterialPtr& material) {
  // Scale Escher's built-in rect mesh to have bounds (0,0),(width,height), then
  // translate it so that it is centered at (0,0).
  // TODO: optimize.
  escher::mat4 rect_transform(1);
  rect_transform[0][0] = width_;
  rect_transform[1][1] = height_;
  rect_transform[3][0] = -0.5 * width_;
  rect_transform[3][1] = -0.5 * height_;
  return escher::Object::NewRect(transform * rect_transform, material);
}

}  // namespace scene_manager
