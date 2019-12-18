// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_SCENE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_SCENE_H_

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree_updater.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/util/validate_eventpair.h"

namespace scenic_impl {
namespace gfx {

class AmbientLight;
class DirectionalLight;
class Light;
class PointLight;
class Scene;
using AmbientLightPtr = fxl::RefPtr<AmbientLight>;
using DirectionalLightPtr = fxl::RefPtr<DirectionalLight>;
using LightPtr = fxl::RefPtr<Light>;
using PointLightPtr = fxl::RefPtr<PointLight>;
using ScenePtr = fxl::RefPtr<Scene>;

class Scene final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Scene(Session* session, SessionId session_id, ResourceId node_id,
        fxl::WeakPtr<ViewTreeUpdater> view_tree_updater, EventReporterWeakPtr event_reporter);
  ~Scene() override;

  bool AddLight(const LightPtr& light, ErrorReporter* error_reporter);
  bool AddAmbientLight(const AmbientLightPtr& light);
  bool AddDirectionalLight(const DirectionalLightPtr& light);
  bool AddPointLight(const PointLightPtr& light);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |Resource|.
  bool Detach(ErrorReporter* error_reporter) override;

  const std::vector<AmbientLightPtr>& ambient_lights() const { return ambient_lights_; }

  const std::vector<DirectionalLightPtr>& directional_lights() const { return directional_lights_; }

  const std::vector<PointLightPtr>& point_lights() const { return point_lights_; }

  const fuchsia::ui::views::ViewRef& view_ref() const { return view_ref_; }
  zx_koid_t view_ref_koid() const;

  fxl::WeakPtr<Scene> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 protected:
  // |Node|
  void OnSceneChanged() override;

 private:
  std::vector<AmbientLightPtr> ambient_lights_;
  std::vector<DirectionalLightPtr> directional_lights_;
  std::vector<PointLightPtr> point_lights_;

  // Control_ref_ and view_ref_ are handles to an entangled eventpair.
  // Control_ref_ is the globally unique handle to one peer, and view_ref_ is the cloneable handle
  // to the other peer.
  // The scene's view_ref_ serves as an element of a focus chain.
  fuchsia::ui::views::ViewRefControl control_ref_;
  fuchsia::ui::views::ViewRef view_ref_;
  zx_koid_t view_ref_koid_ = ZX_KOID_INVALID;

  fxl::WeakPtr<ViewTreeUpdater> view_tree_updater_;

  fxl::WeakPtrFactory<Scene> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_SCENE_H_
