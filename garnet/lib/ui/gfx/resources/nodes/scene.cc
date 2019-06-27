// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/scene.h"

#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/lights/point_light.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Scene::kTypeInfo = {ResourceType::kNode | ResourceType::kScene, "Scene"};

Scene::Scene(Session* session, ResourceId node_id) : Node(session, node_id, Scene::kTypeInfo) {
  scene_ = this;
}

Scene::~Scene() = default;

void Scene::OnSceneChanged() {
  FXL_CHECK(scene_ && scene_->global_id() == global_id())
      << "Error: "
      << "Scene cannot be changed to a different Scene.";
}

bool Scene::AddLight(const LightPtr& light) {
  if (light->IsKindOf<AmbientLight>()) {
    // TODO(SCN-1217): check for duplicates.
    ambient_lights_.push_back(light->As<AmbientLight>());
    return true;
  } else if (light->IsKindOf<DirectionalLight>()) {
    // TODO(SCN-1217): check for duplicates.
    directional_lights_.push_back(light->As<DirectionalLight>());
    return true;
  } else if (light->IsKindOf<PointLight>()) {
    // TODO(SCN-1217): check for duplicates.
    point_lights_.push_back(light->As<PointLight>());
    return true;
  }
  error_reporter()->ERROR() << "scenic::gfx::Scene::AddLight(): unrecognized light type.";
  return false;
}

bool Scene::AddAmbientLight(const AmbientLightPtr& light) {
  ambient_lights_.push_back(light);
  return true;
}

bool Scene::AddDirectionalLight(const DirectionalLightPtr& light) {
  directional_lights_.push_back(light);
  return true;
}

bool Scene::AddPointLight(const PointLightPtr& light) {
  point_lights_.push_back(light);
  return true;
}

bool Scene::Detach() {
  // Skip Node's default implementation; use Resource's instead.
  return Resource::Detach();
}

}  // namespace gfx
}  // namespace scenic_impl
