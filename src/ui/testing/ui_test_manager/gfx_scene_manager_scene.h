// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_GFX_SCENE_MANAGER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_GFX_SCENE_MANAGER_SCENE_H_

#include <lib/sys/component/cpp/testing/realm_builder_types.h>

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
  explicit GfxSceneManagerScene(component_testing::RealmRoot* realm);
  ~GfxSceneManagerScene() override = default;

  // Connects to fuchsia.session.scene.Manager and
  // fuchsia.ui.observation.test.Regsistry.
  void Initialize() override;

  // Connects to fuchsia.ui.app.ViewProvider, and calls
  // fuchsia.session.scene.Manager.SetRootView(), passing the
  // view provider handle as an argument.
  void AttachClientView() override;

  // Uses fuchsia.ui.observation.test.Registry to determine when the client view
  // has been attached to the scene.
  bool ClientViewIsAttached() override;

 private:
  // Service handles, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_SCENE_MANAGER_SCENE_H_
