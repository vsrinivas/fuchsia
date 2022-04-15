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
  GfxSceneManagerScene() = default;
  ~GfxSceneManagerScene() override = default;

  // Connects to fuchsia.session.scene.Manager and
  // fuchsia.ui.observation.test.Regsistry.
  void Initialize(component_testing::RealmRoot* realm) override;

  // Returns true if the client view is connected to the scene.
  // This object can only observe signals on the ui test manager view and the
  // client view holder. It considers the client view attached to the scene when
  // both of the following events have been received:
  //  1. ViewAttachedToScene for ui test manager view.
  //  2. ViewConnected for client view holder.
  bool ClientViewIsAttached() override;

  // Returns true if the is_rendering signal has been received for the client
  // view.
  bool ClientViewIsRendering() override;

  // Returns view ref koid for client view if it's been attached to the scene,
  // or std::nullopt otherwise.
  std::optional<zx_koid_t> ClientViewRefKoid() override;

 private:
  // Service handles, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_SCENE_MANAGER_SCENE_H_
