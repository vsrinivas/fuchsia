// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/lights/directional_light.h"

#include "garnet/lib/ui/scenic/util/error_reporter.h"

namespace scene_manager {

const ResourceTypeInfo DirectionalLight::kTypeInfo = {
    ResourceType::kLight | ResourceType::kDirectionalLight, "DirectionalLight"};

DirectionalLight::DirectionalLight(Session* session, scenic::ResourceId id)
    : Light(session, id, DirectionalLight::kTypeInfo) {}

bool DirectionalLight::SetDirection(const glm::vec3& direction) {
  float length = glm::length(direction);
  if (length < 0.001f) {
    error_reporter()->ERROR() << "scene_manager::DirectionalLight::"
                                 "SetDirection(): length of direction vector "
                                 "is near zero.";
    return false;
  }
  direction_ = direction / length;
  return true;
}

}  // namespace scene_manager
