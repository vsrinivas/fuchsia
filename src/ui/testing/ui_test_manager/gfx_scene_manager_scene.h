// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_GFX_SCENE_MANAGER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_GFX_SCENE_MANAGER_SCENE_H_

#include <fuchsia/session/scene/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <optional>

#include "src/ui/testing/ui_test_manager/gfx_test_view.h"
#include "src/ui/testing/ui_test_manager/ui_test_scene.h"

namespace ui_testing {

// Bridges ViewProvider and scene manager.
class GfxSceneManagerScene : public UITestScene {
 public:
  // Use |realm| to connect to required services.
  // Expects |realm| to expose the following services:
  // * fuchsia.ui.app.ViewProvider
  // * fuchsia.session.scene.Manager
  // * fuchsia.ui.observation.test.Registry
  explicit GfxSceneManagerScene(std::shared_ptr<component_testing::RealmRoot> realm)
      : realm_(realm) {}
  ~GfxSceneManagerScene() override = default;

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
  // Not owned.
  std::shared_ptr<component_testing::RealmRoot> realm_;

  // Manages the ui test manager's view in the scene.
  std::unique_ptr<GfxTestView> test_view_;

  // Scene manager connection.
  fuchsia::session::scene::ManagerPtr scene_manager_;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_SCENE_MANAGER_SCENE_H_
