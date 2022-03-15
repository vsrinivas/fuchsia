// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_

#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/ui/testing/ui_test_manager/ui_test_scene.h"

namespace ui_testing {

// Library class to manage test realm and scene setup on behalf of UI
// integration test clients.
class UITestManager {
 public:
  enum class SceneOwnerType {
    ROOT_PRESENTER = 1,

    SCENE_MANAGER = 2,

    // Test manager builds the root of the scene directly.
    TEST_MANAGER = 3,
  };

  enum class InputOwnerType {
    ROOT_PRESENTER = 1,

    INPUT_PIPELINE = 2,

    // Test fixture interacts directly with scenic input APIs.
    TEST_FIXTURE = 3,
  };

  struct Config {
    SceneOwnerType scene_owner;
    InputOwnerType input_owner;
    bool has_view_provider;

    // List of services the test manager needs to expose
    // to the test fixture.
    std::vector<std::string> exposed_services;

    bool use_flatland;
  };

  UITestManager() = default;
  ~UITestManager() = default;

  // Initialize the test realm, but do NOT build yet.
  void Init(Config config);

  // Adds a child to the realm under construction, and returns a pointer
  // to the new child.
  // Must NOT be called after BuildRealm().
  component_testing::Realm* AddSubrealm();

  // Calls realm_builder_.Build();
  void BuildRealm();

  // Returns a clone of the realm's exposed services directory.
  // Clients should call this method once, and retain the handle returned.
  //
  // MUST be called AFTER BuildRealm().
  std::unique_ptr<sys::ServiceDirectory> TakeExposedServicesDirectory();

  // Creates the root of the scene (either via scene manager or root presenter,
  // OR by direct construction).
  //
  // MUST be called AFTER BuildRealm().
  void InitializeScene();

  // Attaches a client view.
  // The test manager will request for the client to create its view via
  // fuchsia.ui.app.ViewProvider. The client's subrealm MUST expose
  // fuchsia.ui.app.ViewProvider.
  //
  // MUST be called AFTER InitializeScene().
  void AttachClientView();

  // Returns true if the client view is attached to the scene.
  // In order to be consider "attached to the scene", there must be a connected
  // path from the scene root to the client view, and the client view must have
  // presented at least one frame of content.
  // Returns false otherwise.
  void ClientViewIsAttached();

 private:
  Config config_;
  component_testing::RealmBuilder realm_builder_ = component_testing::RealmBuilder::Create();
  std::unique_ptr<component_testing::RealmRoot> realm_root_;
  std::unique_ptr<UITestScene> scene_;

  // Add state as necessary.

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(UITestManager);
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_
