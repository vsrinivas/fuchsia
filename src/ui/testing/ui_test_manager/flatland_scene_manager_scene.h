// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_SCENE_MANAGER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_SCENE_MANAGER_SCENE_H_

#include <lib/sys/component/cpp/testing/realm_builder_types.h>

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
  explicit FlatlandSceneManagerScene(component_testing::RealmRoot* realm);
  ~FlatlandSceneManagerScene() override = default;

  // Connects to fuchsia.session.scene.Manager.
  void Initialize() override;

  // Calls fuchsia.ui.scene.Manager.SetRootView, passing a handle to the
  // realm's view provider. Scene manager will in turn call CreateView() on the
  // view provider.
  void AttachClientView() override;

  // Returns true once client view ref is received. Returns false before.
  bool ClientViewIsAttached() override;

 private:
  // Service handles, client view state, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_FLATLAND_SCENE_MANAGER_SCENE_H_
