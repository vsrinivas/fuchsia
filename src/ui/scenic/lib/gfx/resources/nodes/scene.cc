// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"

#include <lib/trace/event.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"
#include "src/ui/scenic/lib/gfx/resources/lights/ambient_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/directional_light.h"
#include "src/ui/scenic/lib/gfx/resources/lights/point_light.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Scene::kTypeInfo = {ResourceType::kNode | ResourceType::kScene, "Scene"};

Scene::Scene(Session* session, SessionId session_id, ResourceId node_id,
             fxl::WeakPtr<ViewTreeUpdater> view_tree_updater, EventReporterWeakPtr event_reporter)
    : Node(session, session_id, node_id, Scene::kTypeInfo),
      view_tree_updater_(view_tree_updater),
      weak_factory_(this) {
  scene_ = this;

  {
    // Safe and valid eventpair, by construction.
    zx_status_t status = zx::eventpair::create(
        /*flags*/ 0u, &control_ref_.reference, &view_ref_.reference);
    FX_DCHECK(status == ZX_OK);
    // Remove signaling.
    status = view_ref_.reference.replace(ZX_RIGHTS_BASIC, &view_ref_.reference);
    FX_DCHECK(status == ZX_OK);
    FX_DCHECK(validate_viewref(control_ref_, view_ref_));

    view_ref_koid_ = fsl::GetKoid(view_ref_.reference.get());
    FX_DCHECK(view_ref_koid_ != ZX_KOID_INVALID);
  }

  {
    TRACE_DURATION_BEGIN("gfx", "ResourceCtorViewRefClone");
    fuchsia::ui::views::ViewRef clone;
    fidl::Clone(view_ref_, &clone);
    TRACE_DURATION_END("gfx", "ResourceCtorViewRefClone");

    // Scene may *always* receive focus when connected to a compositor. If it is not actually
    // connected to a compositor, the request-focus policy will ensure it is never sent focus.
    fit::function<bool()> may_receive_focus = [] { return true; };
    // Scene is *never* input suppressed.
    fit::function<bool()> is_input_suppressed = [] { return false; };
    fit::function<std::optional<glm::mat4>()> global_transform = [weak_ptr = GetWeakPtr()] {
      return weak_ptr ? std::optional<glm::mat4>{weak_ptr->GetGlobalTransform()} : std::nullopt;
    };
    fit::function<void(ViewHolderPtr)> add_annotation_view_holder = [](auto) {
      FX_NOTREACHED() << "Cannot create Annotation ViewHolder for Scene.";
    };
    fit::function<void(const escher::ray4& world_space_ray, HitAccumulator<ViewHit>* accumulator,
                       bool semantic_hit_test)>
        hit_test = [weak_ptr = GetWeakPtr()](const escher::ray4& world_space_ray,
                                             HitAccumulator<ViewHit>* accumulator,
                                             bool semantic_hit_test) {
          if (weak_ptr) {
            HitTest(weak_ptr.get(), world_space_ray, accumulator, semantic_hit_test);
          }
        };

    FX_DCHECK(session_id != 0u) << "GFX-side invariant for ViewTree";
    if (view_tree_updater_) {
      view_tree_updater_->AddUpdate(
          ViewTreeNewRefNode{.view_ref = std::move(clone),
                             .event_reporter = std::move(event_reporter),
                             .may_receive_focus = std::move(may_receive_focus),
                             .is_input_suppressed = std::move(is_input_suppressed),
                             .global_transform = std::move(global_transform),
                             .hit_test = std::move(hit_test),
                             .add_annotation_view_holder = std::move(add_annotation_view_holder),
                             .session_id = session_id});
    }
  }
  // NOTE: Whether or not this Scene is connected to the Compositor CANNOT be determined here (and
  // hence we can't push ViewTreeMakeRoot(koid)). Instead, the session updater must determine which
  // Scene is connected and explicitly make that Scene the root of the ViewTree.
}

Scene::~Scene() {
  if (view_tree_updater_) {
    view_tree_updater_->AddUpdate(ViewTreeDeleteNode({.koid = view_ref_koid_}));
  }
}

void Scene::OnSceneChanged() {
  FX_CHECK(scene_ && scene_->global_id() == global_id())
      << "Error: "
      << "Scene cannot be changed to a different Scene.";
}

bool Scene::AddLight(const LightPtr& light, ErrorReporter* error_reporter) {
  if (light->IsKindOf<AmbientLight>()) {
    // TODO(fxbug.dev/24420): check for duplicates.
    ambient_lights_.push_back(light->As<AmbientLight>());
    return true;
  } else if (light->IsKindOf<DirectionalLight>()) {
    // TODO(fxbug.dev/24420): check for duplicates.
    directional_lights_.push_back(light->As<DirectionalLight>());
    return true;
  } else if (light->IsKindOf<PointLight>()) {
    // TODO(fxbug.dev/24420): check for duplicates.
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
