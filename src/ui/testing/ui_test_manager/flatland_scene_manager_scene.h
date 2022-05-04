// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_SCENE_MANAGER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_SCENE_MANAGER_SCENE_H_

#include <fuchsia/session/scene/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <optional>

#include "src/ui/testing/ui_test_manager/ui_test_scene.h"

namespace ui_testing {

// Owns a connection to scene manager.
// Flatland scene manager's implementation of
// fuchsia.session.scene.Manager.SetRootView() blocks until the client's view is
// attached to the scene. So, the test does not need to own its own view to
// determine when the scene is fully connected. Instead, it can simply wait for
// SetRootView() to return. Thus, we can attach the test view provider's view
// directly to scene manager's root hierarchy.
//
// scene root hierarchy (owned by scene manager)
//                      |
//                      V
// test view provider's view (owned by view provider)
class FlatlandSceneManagerScene : public UITestScene {
 public:
  // Use |realm| to connect to required services.
  // Expects |realm| to expose the following services:
  // * fuchsia.ui.app.ViewProvider
  // * fuchsia.session.scene.Manager
  explicit FlatlandSceneManagerScene(std::shared_ptr<component_testing::RealmRoot> realm);
  ~FlatlandSceneManagerScene() override = default;

  // |UITestScene|
  void Initialize() override;

  // |UITestScene|
  bool ClientViewIsAttached() override;

  // |UITestScene|
  bool ClientViewIsRendering() override;

  // |UITestScene|
  std::optional<zx_koid_t> ClientViewRefKoid() override;

  // |UITestScene|
  float ClientViewScaleFactor() override;

 private:
  std::shared_ptr<component_testing::RealmRoot> realm_;
  fuchsia::session::scene::ManagerPtr scene_manager_;
  std::optional<fuchsia::ui::views::ViewRef> client_view_ref_;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_SCENE_MANAGER_SCENE_H_
