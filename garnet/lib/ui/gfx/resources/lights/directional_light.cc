// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"

#include "garnet/lib/ui/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo DirectionalLight::kTypeInfo = {
    ResourceType::kLight | ResourceType::kDirectionalLight, "DirectionalLight"};

DirectionalLight::DirectionalLight(Session* session, ResourceId id)
    : Light(session, id, DirectionalLight::kTypeInfo) {}

bool DirectionalLight::SetDirection(const glm::vec3& direction, ErrorReporter* reporter) {
  float length = glm::length(direction);
  if (length < 0.001f) {
    reporter->ERROR() << "scenic::gfx::DirectionalLight::SetDirection(): length of direction "
                         "vector is near zero.";
    return false;
  }
  direction_ = direction / length;
  return true;
}

}  // namespace gfx
}  // namespace scenic_impl
