// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_GFX_TEST_MANAGER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_GFX_TEST_MANAGER_SCENE_H_

#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include "src/ui/testing/ui_test_manager/ui_test_scene.h"

namespace ui_testing {

// Directly owns the root of the scene.
//
// Handles attaching the client view to the scene, and determining when the
// scene is fully connected. Scene topology:
//
// scene root (owned by GfxTestManagerScene)
//                     |
//                     V
// client view holder (owned by GfxTestManagerScene)
//                     |
//                     V
// client view (owned by client)
class GfxTestManagerScene : public UITestScene {
 public:
  // Use |realm| to connect to required services.
  // Expects |realm| to expose the following services:
  // * fuchsia.ui.app.ViewProvider
  // * fuchsia.ui.scenic.Scenic
  explicit GfxTestManagerScene(component_testing::RealmRoot* realm);
  ~GfxTestManagerScene() override = default;

  // Creates scene root.
  void Initialize() override;

  // Creates client view holder, and attaches client view using
  // fuchsia.ui.app.ViewProvider.
  void AttachClientView() override;

  // Uses fuchsia.ui.observation.test.Registry to determine when the client view
  // has been attached to the scene.
  bool ClientViewIsAttached() override;

 private:
  // Service handles, scenic session resources, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_TEST_MANAGER_SCENE_H_
