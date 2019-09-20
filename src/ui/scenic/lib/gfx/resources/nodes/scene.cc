// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"

#include "garnet/public/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/gfx/resources/lights/ambient_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/directional_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/point_light.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Scene::kTypeInfo = {ResourceType::kNode | ResourceType::kScene, "Scene"};

Scene::Scene(Session* session, SessionId session_id, ResourceId node_id)
    : Node(session, session_id, node_id, Scene::kTypeInfo),
      gfx_session_(session),
      weak_factory_(this) {
  FXL_DCHECK(gfx_session_);
  scene_ = this;

  {
    // Safe and valid eventpair, by construction.
    zx_status_t status = zx::eventpair::create(
        /*flags*/ 0u, &control_ref_.reference, &view_ref_.reference);
    FXL_DCHECK(status == ZX_OK);
    // Remove signaling.
    status = view_ref_.reference.replace(ZX_RIGHTS_BASIC, &view_ref_.reference);
    FXL_DCHECK(status == ZX_OK);
    FXL_DCHECK(validate_viewref(control_ref_, view_ref_));

    view_ref_koid_ = fsl::GetKoid(view_ref_.reference.get());
    FXL_DCHECK(view_ref_koid_ != ZX_KOID_INVALID);
  }

  fuchsia::ui::views::ViewRef clone;
  fidl::Clone(view_ref_, &clone);
  gfx_session_->view_tree_updates().push_back(ViewTreeNewRefNode{.view_ref = std::move(clone)});
  // NOTE: Whether or not this Scene is connected to the Compositor CANNOT be determined here (and
  // hence we can't push ViewTreeMakeRoot(koid)). Instead, the session updater must determine which
  // Scene is connected and explicitly make that Scene the root of the ViewTree.
}

Scene::~Scene() {
  gfx_session_->view_tree_updates().push_back(ViewTreeDeleteNode({.koid = view_ref_koid_}));
}

void Scene::OnSceneChanged() {
  FXL_CHECK(scene_ && scene_->global_id() == global_id())
      << "Error: "
      << "Scene cannot be changed to a different Scene.";
}

bool Scene::AddLight(const LightPtr& light, ErrorReporter* error_reporter) {
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
  error_reporter->ERROR() << "scenic::gfx::Scene::AddLight(): unrecognized light type.";
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

bool Scene::Detach(ErrorReporter* error_reporter) {
  // Skip Node's default implementation; use Resource's instead.
  return Resource::Detach(error_reporter);
}

zx_koid_t Scene::view_ref_koid() const { return view_ref_koid_; }

}  // namespace gfx
}  // namespace scenic_impl
