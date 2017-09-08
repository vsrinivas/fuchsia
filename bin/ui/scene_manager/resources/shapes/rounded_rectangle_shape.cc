// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/shapes/rounded_rectangle_shape.h"

#include "escher/shape/mesh.h"
#include "escher/shape/rounded_rect_factory.h"

namespace scene_manager {

const ResourceTypeInfo RoundedRectangleShape::kTypeInfo = {
    ResourceType::kShape | ResourceType::kRoundedRectangle,
    "RoundedRectangleShape"};

RoundedRectangleShape::RoundedRectangleShape(
    Session* session,
    scenic::ResourceId id,
    const escher::RoundedRectSpec& spec,
    escher::MeshPtr mesh)
    : PlanarShape(session, id, RoundedRectangleShape::kTypeInfo),
      spec_(spec),
      mesh_(std::move(mesh)) {}

bool RoundedRectangleShape::ContainsPoint(const escher::vec2& point) const {
  return spec_.ContainsPoint(point);
}

escher::Object RoundedRectangleShape::GenerateRenderObject(
    const escher::mat4& transform,
    const escher::MaterialPtr& material) {
  return escher::Object(transform, mesh_, material);
}

}  // namespace scene_manager
