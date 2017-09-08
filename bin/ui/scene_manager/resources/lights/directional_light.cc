// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/lights/directional_light.h"

namespace scene_manager {

const ResourceTypeInfo DirectionalLight::kTypeInfo = {
    ResourceType::kDirectionalLight, "DirectionalLight"};

DirectionalLight::DirectionalLight(Session* session,
                                   scenic::ResourceId id,
                                   const escher::vec3& direction,
                                   float intensity)
    : Resource(session, id, DirectionalLight::kTypeInfo),
      direction_(direction),
      intensity_(intensity) {}

}  // namespace scene_manager
