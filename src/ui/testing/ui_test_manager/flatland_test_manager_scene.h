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
  FlatlandTestManagerScene();
  ~FlatlandTestManagerScene() override = default;

  // Creates flatland display and root view, and then attaches a client view as its
  // descendant via fuchsia.ui.app.ViewProvider.CreateView().
  void Initialize(component_testing::RealmRoot* realm) override;

  // Returns true if the client view is connected to the scene.
  bool ClientViewIsAttached() override;

  // Returns true if the client view is connected to the scene, and has rendered
  // at least one frame of content.
  bool ClientViewIsRendering() override;

  // Returns the view ref koid for the client view if it's been attached to the
  // scene, and std::nullopt otherwise.
  std::optional<zx_koid_t> ClientViewRefKoid() override;

 private:
  // Service handles, flatland resources, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_TEST_MANAGER_SCENE_H_
