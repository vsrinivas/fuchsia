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
  explicit GfxTestManagerScene(std::shared_ptr<component_testing::RealmRoot> realm);
  ~GfxTestManagerScene() override = default;

  // |UITestScene|
  void Initialize() override;

  // |UITestScene|
  bool ClientViewIsAttached() override;

  // |UITestScene|
  bool ClientViewIsRendering() override;

  // |UITestScene|
  std::optional<zx_koid_t> ClientViewRefKoid() override;

 private:
  // Service handles, scenic session resources, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_TEST_MANAGER_SCENE_H_
