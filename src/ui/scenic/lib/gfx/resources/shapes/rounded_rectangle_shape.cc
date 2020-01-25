// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/shapes/rounded_rectangle_shape.h"

#include "src/ui/lib/escher/shape/mesh.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo RoundedRectangleShape::kTypeInfo = {
    ResourceType::kShape | ResourceType::kRoundedRectangle, "RoundedRectangleShape"};

RoundedRectangleShape::RoundedRectangleShape(Session* session, SessionId session_id, ResourceId id,
                                             const escher::RoundedRectSpec& spec)
    : PlanarShape(session, session_id, id, RoundedRectangleShape::kTypeInfo), spec_(spec) {}

bool RoundedRectangleShape::ContainsPoint(const escher::vec2& point) const {
  return spec_.ContainsPoint(point);
}

escher::Object RoundedRectangleShape::GenerateRenderObject(const escher::mat4& transform,
                                                           const escher::MaterialPtr& material) {
  return escher::Object(transform, nullptr, material);
}

}  // namespace gfx
}  // namespace scenic_impl
