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
  GfxRootPresenterScene() = default;
  ~GfxRootPresenterScene() override = default;

  // Connecs to fuchsia.ui.policy.Presenter and
  // fuchsia.ui.observation.test.Registry.
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

  // Returns the koid for the client's view ref if it's been set, or
  // std::nullopt otherwise.
  //
  // The koid may be available before the client view is attached to the scene,
  // so the return value should NOT be used as a "scene ready" signal.
  std::optional<zx_koid_t> ClientViewRefKoid() override;

 private:
  // Not owned.
  component_testing::RealmRoot* realm_;

  // Scenic session resources.
  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;

  // Test view and client view's ViewHolder.
  std::unique_ptr<scenic::ViewHolder> client_view_holder_;
  std::unique_ptr<scenic::View> ui_test_manager_view_;
  std::optional<fuchsia::ui::views::ViewRef> client_view_ref_;

  bool test_view_attached_ = false;
  bool client_view_connected_ = false;
  bool client_view_is_rendering_ = false;
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_GFX_ROOT_PRESENTER_SCENE_H_
