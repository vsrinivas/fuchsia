// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/lights/point_light.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo PointLight::kTypeInfo = {ResourceType::kLight | ResourceType::kPointLight,
                                                "PointLight"};

PointLight::PointLight(Session* session, ResourceId id)
    : Light(session, id, PointLight::kTypeInfo) {}

bool PointLight::SetPosition(const glm::vec3& position) {
  position_ = position;
  return true;
}

bool PointLight::SetFalloff(float falloff) {
  falloff_ = falloff;
  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
