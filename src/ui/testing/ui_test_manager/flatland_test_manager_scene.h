// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_TEST_MANAGER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_TEST_MANAGER_SCENE_H_

#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include "src/ui/testing/ui_test_manager/ui_test_scene.h"

namespace ui_testing {

// Directly owns the flatland display.
//
// Handles attaching the client view to the scene, and determining when the
// scene is fully connected. Scene topology:
//
// flatland display (owned by FlatlandTestManagerScene)
//                     |
//                     V
// root view (owned by FlatlandTestManagerScene)
//                     |
//                     V
// client viewport (owned by FlatlandTestManagerScene)
//                     |
//                     V
// client view (owned by view provider)
class FlatlandTestManagerScene : public UITestScene {
 public:
  // Use |realm| to connect to required services.
  // Expects |realm| to expose the following services:
  // * fuchsia.ui.app.ViewProvider
  // * fuchsia.ui.composition.Flatland
  explicit FlatlandTestManagerScene(component_testing::RealmRoot* realm);
  ~FlatlandTestManagerScene() override = default;

  // Creates flatland display and root view, and then attaches a client view as its
  // descendant via fuchsia.ui.app.ViewProvider.CreateView().
  void Initialize() override;

  // Connects to fuchsia.ui.app.ViewProvider, and calls CreateView().
  // Also creates a corresponding viewport (see hierarchy above).
  void AttachClientView() override;

  // Returns true after the client viewref is received, and false before.
  bool ClientViewIsAttached() override;

 private:
  // Service handles, flatland resources, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_TEST_MANAGER_SCENE_H_
