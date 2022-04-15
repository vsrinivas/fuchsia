// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_

#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <optional>

#include "src/lib/fxl/macros.h"
#include "src/ui/testing/ui_test_manager/ui_test_scene.h"

namespace ui_testing {

// Library class to manage test realm and scene setup on behalf of UI
// integration test clients.
//
// TEST REALM
//
// One of UITestManager's chief responsibilities is to own a RealmBuilder realm
// encapsulating the relevant portion of the UI stack. The realm comprises two
// main parts:
//
//   (1) The ui layer component. This component runs the portion of the UI stack
//       specified by the client via the UITestManager::Config argument passed
//       to the UITestManager constructor. This portion of the realm is
//       specified statically in //src/ui/testing/ui_test_manager/meta.
//   (2) The client subrealm. This subrealm is a RealmBuilder Realm, owned and
//       configured by the client, containing any additional test-sepcific
//       components.
//
// The component topology of the test is:
//
//                        test_manager
//                       /            \
//         test fixture component      realm builder server
//                   /
//       ui test manager realm root
//           /             \
//   client subrealm     ui layer component
//          |                   |
//    (test-specific       (ui components)
//      components)
//
// Clients can configure the scene owner, which specifies which ui layer
// component to use (layers are specified statically in
// //src/ui/testing/ui_test_manager/meta). Clients can also specify the set of
// ui services that must be routed to the client subrealm, and the set of client
// services that must be exposed out of the top-level realm. UITestManager will
// configure all necessary routes between the ui layer component, the client
// subrealm, and the top-level realm.
//
// CLIENT SUBREALM
//
// The client can specify the test-specific portion of the component topology
// within its own subrealm (see AddSubrealm() below). Important notes on the client
// subrealm:
//
//   (1) A client subrealm should NOT contain any UI services (scenic, root
//   presenter, scene manager, input pipeline, text manager, or a11y manager).
//   (2) A client MUST expose fuchsia.ui.app.ViewProvider from its subrealm if
//       specifies a scene owner.
//   (3) Clients can consume required ui services from ParentRef(), provided
//       they request those services in Config::ui_to_client_services.
//
// SCENE SETUP
//
// UITestManager's second chief responsibility is to coordinate scene setup. It
// fulfills a similar role to session manager by briding the client view
// provider and the scene owner (root presenter or scene manager). Clients do
// NOT need to use the scene ownership APIs (fuchsia.ui.policy.Presenter and
// fuchsia.session.scene.Manager) directly; instead, they can rely on
// UITestManager to handle those details.
//
// UITestManager also handles some of the scene setup synchronization on behalf
// of clients. It allows clients to observe two states of their test view (i.e.
// the view created by the ViewProvider implementation the client exposes from
// its subrealm):
//
//   (1) "attached": In this case, there is a fully connected path from the
//       scene root to the client view.
//   (2) "rendering": In this case, the client view is "attached" per (1) AND
//       has rendered at least one frame of content.
//
// Clients can gate on one of these two signals as appropriate to guarantee that
// the scene is in a sensible state before proceeding with test logic.
//
// EXAMPLE USAGE
//
// ```
// // Configure UITestManger instance.
// UITestManager::Config config;
// config.scene_owner = UITestManager::SceneOwnerType::ROOT_PRESENTER;
// config.ui_to_client_services = { fuchsia::ui::scenic::Scenic::Name_ };
// UITestManager ui_test_manager(std::move(config));
//
// // Add a client subrealm, and configure. This step must happen before calling
// // BuildRealm().
// auto client_subrealm = ui_test_manager.AddSubrealm();
//
// // Add a view provider to the client subrealm.
// client_subrealm.AddChild(kViewProvider, kViewProviderUrl);
//
// // Expose the view provider service out of the subrealm.
// client_subrealm.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::app::ViewProvider::Name_}},
//                            .source = ChildRef{kViewProvider},
//                            .targets = {ParentRef()}});
//
// // Consume scenic from the ui layer. UITestManager routes this service from the ui layer
// // component to the client subrealm, so we consume it from ParentRef() within
// // the subrealm.
// client_subrealm.AddRoute(Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_}},
//                            .source = ParentRef(),
//                            .targets = {ChildRef{kViewProvider}}});
//
// // Build the realm, and take a copy of the exposed services directory.
// ui_test_manager.BuildRealm();
// auto realm_exposed_services = ui_test_manger.TakeExposedServicesDirectory();
//
// // Create a test view, and attach it to the scene.
// ui_test_manager.InitializeScene();
//
// // Wait until the client view is rendering to proceed with the test case.
// RunLoopUntil([&ui_test_manager](){
//   return ui_test_manager.ClientViewIsRendering();
// });
//
// // Connect to some service in the test realm to drive the test.
// auto service = realm_exposed_services.Connect<...>();
// service->...;
// ```
class UITestManager {
 public:
  enum class SceneOwnerType {
    ROOT_PRESENTER = 1,

    SCENE_MANAGER = 2,
  };

  struct Config {
    // Specifies the entity that owns the root of the scene.
    // If std::nullopt, then no scene owner will be present in the test realm.
    //
    // For now, UITestManager assumes that the entity that input pipeline owns
    // input if scene_owner is not std::nullopt. We may revisit this assumption
    // if the need arises.
    //
    // Furthermore, if a scene owner is specified, the client promises to expose
    // fuchsia.ui.app.ViewProvider from its subrealm.
    std::optional<SceneOwnerType> scene_owner;

    // List of ui services required by the client.
    // UITestManager will route these services from the ui layer component to the
    // client subrealm.
    std::vector<std::string> ui_to_client_services;

    // List of non-ui services the test manager needs to expose to the test fixture.
    // By specifying services here, the client promises to expose them from its subrealm.
    //
    // This field should not be required for most use cases.
    std::vector<std::string> exposed_client_services;

    bool use_flatland = false;
  };

  explicit UITestManager(Config config);
  ~UITestManager() = default;

  // Adds a child to the realm under construction, and returns the new child.
  // Must NOT be called after BuildRealm().
  component_testing::Realm AddSubrealm();

  // Calls realm_builder_.Build();
  void BuildRealm();

  // Returns a clone of the realm's exposed services directory.
  // Clients should call this method once, and retain the handle returned.
  //
  // MUST be called AFTER BuildRealm().
  std::unique_ptr<sys::ServiceDirectory> TakeExposedServicesDirectory();

  // Creates the root of the scene (either via scene manager or root presenter,
  // OR by direct construction), and attaches the client view via
  // fuchsia.ui.app.ViewProvider.
  //
  // MUST be called AFTER BuildRealm().
  void InitializeScene();

  // Returns true if the client view is connected to the scene.
  bool ClientViewIsAttached();

  // Returns true if the client view is connected to the scene, and has rendered
  // at least one frame of content.
  bool ClientViewIsRendering();

  // Returns the view ref koid of the client view if it's been attached to the
  // scene, and std::nullopt otherwise.
  std::optional<zx_koid_t> ClientViewRefKoid();

 private:
  void SetUseFlatlandConfig(bool use_flatland);

  // Helper methods to configure the test realm.
  void AddBaseRealmComponent();
  void ConfigureDefaultSystemServices();
  void ConfigureSceneOwner();
  void ConfigureInput();
  void ConfigureScenic();

  Config config_;
  component_testing::RealmBuilder realm_builder_ = component_testing::RealmBuilder::Create();
  std::unique_ptr<component_testing::RealmRoot> realm_root_;
  std::unique_ptr<UITestScene> scene_;

  // Add state as necessary.

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(UITestManager);
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_
