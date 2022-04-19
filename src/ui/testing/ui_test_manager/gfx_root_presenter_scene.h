// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_GFX_ROOT_PRESENTER_SCENE_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_GFX_ROOT_PRESENTER_SCENE_H_

#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <optional>

#include "src/ui/testing/ui_test_manager/gfx_test_view.h"
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
  explicit GfxRootPresenterScene(std::shared_ptr<component_testing::RealmRoot> realm)
      : realm_(realm) {}
  ~GfxRootPresenterScene() override = default;

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
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_ROOT_PRESENTER_SCENE_H_
