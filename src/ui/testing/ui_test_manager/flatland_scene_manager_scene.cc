// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/ui_test_manager/flatland_scene_manager_scene.h"

#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace ui_testing {

FlatlandSceneManagerScene::FlatlandSceneManagerScene(
    std::shared_ptr<component_testing::RealmRoot> realm)
    : realm_(realm) {}

void FlatlandSceneManagerScene::Initialize() {
  scene_manager_ = realm_->Connect<fuchsia::session::scene::Manager>();
  auto view_provider = realm_->Connect<fuchsia::ui::app::ViewProvider>();
  scene_manager_->SetRootView(std::move(view_provider),
                              [this](fuchsia::ui::views::ViewRef view_ref) {
                                FX_LOGS(INFO) << "Client view is rendering";
                                client_view_ref_ = std::move(view_ref);
                              });
}

bool FlatlandSceneManagerScene::ClientViewIsAttached() {
  // Implement when we have a use case.
  return false;
}

bool FlatlandSceneManagerScene::ClientViewIsRendering() {
  // Scene manager waits to return the client view ref from SetRootView() until
  // the client view has presented at least one frame of content.
  return client_view_ref_.has_value();
}

std::optional<zx_koid_t> FlatlandSceneManagerScene::ClientViewRefKoid() {
  if (!client_view_ref_)
    return std::nullopt;

  return fsl::GetKoid(client_view_ref_->reference.get());
}

float FlatlandSceneManagerScene::ClientViewScaleFactor() {
  // Implement when we have a use case.
  return 1.f;
}

}  // namespace ui_testing
