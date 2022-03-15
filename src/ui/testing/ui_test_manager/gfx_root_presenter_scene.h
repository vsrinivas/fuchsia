// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_GFX_ROOT_PRESENTER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_GFX_ROOT_PRESENTER_SCENE_H_

#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include "src/ui/testing/ui_test_manager/ui_test_scene.h"

namespace ui_testing {

// Bridges root presenter and the view provider.
// Handles attaching the client view to the scene, and determining when the
// client view is attached.
class GfxRootPresenterScene : public UITestScene {
 public:
  // Use |realm| to connect to required services.
  // Expects |realm| to expose the following services:
  // * fuchsia.ui.app.ViewProvider (if attaching a view).
  // * fuchsia.ui.policy.Presenter
  // * fuchsia.ui.observation.test.Registry
  explicit GfxRootPresenterScene(component_testing::RealmRoot* realm);
  ~GfxRootPresenterScene() override = default;

  // Connecs to fuchsia.ui.policy.Presenter and
  // fuchsia.ui.observation.test.Registry.
  void Initialize() override;

  // Connects to fuchsia.ui.app.ViewProvider, and calls CreateViewWithViewRef().
  // Then, presents the view to root presenter via fuchsia.ui.policy.Presenter.
  void AttachClientView() override;

  // Uses fuchsia.ui.observation.test.Registry to determine when the ViewRef
  // used in AttachClientView() is present in the scene.
  bool ClientViewIsAttached() override;

 private:
  // Service handles, etc.
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_ROOT_PRESENTER_SCENE_H_
