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
  explicit FlatlandTestManagerScene(std::shared_ptr<component_testing::RealmRoot> realm);
  ~FlatlandTestManagerScene() override = default;

  // |UITestScene|
  void Initialize() override;

  // |UITestScene|
  bool ClientViewIsAttached() override;

  // |UITestScene|
  bool ClientViewIsRendering() override;

  // |UITestScene|
  std::optional<zx_koid_t> ClientViewRefKoid() override;

 private:
  // Service handles, flatland resources, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_TEST_MANAGER_SCENE_H_
