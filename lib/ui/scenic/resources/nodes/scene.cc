// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/nodes/scene.h"

#include "garnet/lib/ui/scenic/resources/lights/ambient_light.h"
#include "garnet/lib/ui/scenic/resources/lights/directional_light.h"

namespace scene_manager {

const ResourceTypeInfo Scene::kTypeInfo = {
    ResourceType::kNode | ResourceType::kScene, "Scene"};

Scene::Scene(Session* session, scenic::ResourceId node_id)
    : Node(session, node_id, Scene::kTypeInfo) {}

Scene::~Scene() = default;

bool Scene::AddLight(const LightPtr& light) {
  if (light->IsKindOf<AmbientLight>()) {
    // TODO: check for duplicates.
    ambient_lights_.push_back(
        AmbientLightPtr(static_cast<AmbientLight*>(light.get())));
    return true;
  } else if (light->IsKindOf<DirectionalLight>()) {
    // TODO: check for duplicates.
    directional_lights_.push_back(
        DirectionalLightPtr(static_cast<DirectionalLight*>(light.get())));
    return true;
  }
  error_reporter()->ERROR()
      << "scene_manager::Scene::AddLight(): unrecognized light type.";
  return false;
}

bool Scene::Detach() {
  // Skip Node's default implementation; use Resource's instead.
  return Resource::Detach();
}

}  // namespace scene_manager
