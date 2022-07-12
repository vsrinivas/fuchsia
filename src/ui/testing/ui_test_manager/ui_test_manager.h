// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_
#define SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_

#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/test/scene/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <optional>

#include "src/lib/fxl/macros.h"

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
// INPUT
//
// UITestManager enables configurations with or without input.
//
// * If clients specify a scene owner via Config::scene_owner and set
//   Config::use_input = true, then UITestManager assumes input pipeline will
//   own input for the test scene (either as a standalone component with root
//   presenter or as part of scene manager).
// * If a client does not specify a scene owner, but sets Config::use_input = true,
//  then UITestManager will expose raw scenic input APIs out of the test realm.
// * If clients set Config::use_input = false, then UITestManager will not
//   any input APIs out of the test realm.
//
// ACCESSIBILITY
//
// UITestManager enables configurations without accessibility, and also allows
// clients to opt into using a real or fake a11y manager. In general, clients
// should not request accessibility unless it's explicitly required. For cases
// where accessibility is required, clients should prefer using the fake a11y
// manager for tests that require a11y services, but do not test a11y
// functionality (e.g. tests that run a chromium view). Clients should only use
// a real a11y manager for tests that explicitly exercise accessibility-specific
// behavior.
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
class UITestManager : public fuchsia::ui::focus::FocusChainListener {
 public:
  enum class SceneOwnerType {
    ROOT_PRESENTER = 1,

    SCENE_MANAGER = 2,
  };

  enum class AccessibilityOwnerType {
    // Use the fake a11y manager. Clients should prefer using the fake a11y
    // manager for tests that require a11y services, but do not test a11y
    // functionality (e.g. tests that run a chromium client).
    FAKE = 1,

    // Use the real a11y manager. Clients should only use the real a11y manager
    // for tests that exercise accessibility-specific functionality.
    REAL = 2,
  };

  struct Config {
    // Specifies the entity that owns the root of the scene, if any.
    // If std::nullopt, then no scene owner will be present in the test realm.
    //
    // For now, UITestManager assumes that the entity that input pipeline owns
    // input if scene_owner is not std::nullopt. We may revisit this assumption
    // if the need arises.
    //
    // Furthermore, if a scene owner is specified, the client promises to expose
    // fuchsia.ui.app.ViewProvider from its subrealm.
    std::optional<SceneOwnerType> scene_owner;

    // Specifies the entity that owns accessibility in the test realm, if any.
    // If std::nullopt, then no a11y services will be present in the test realm.
    std::optional<AccessibilityOwnerType> accessibility_owner;

    // Instructs UITestManager to expose input APIs out of the test realm.
    //
    // If |scene_owner| has a value, input pipeline will own input and
    // the top-level realm will expose the following services:
    //   * fuchsia.input.injection.InputDeviceRegistry
    //   * fuchsia.ui.policy.DeviceListenerRegistry
    //   * fuchsia.ui.pointerinjector.configuration.Setup
    //
    // If |scene_owner| is std::nullopt, the top-level realm exposes the raw scenic
    // input API:
    //   * fuchsia.ui.pointerinjector.Registry
    bool use_input = false;

    // List of ui services required by components in the client subrealm.
    // UITestManager will route these services from the ui layer component to the
    // client subrealm.
    std::vector<std::string> ui_to_client_services;

    // List of non-ui services the test manager needs to expose to the test fixture.
    // By specifying services here, the client promises to expose them from its subrealm.
    std::vector<std::string> exposed_client_services;

    // List of client realm services to route to the ui layer component.
    //
    // *** Use cases for this field are ~very~ rare.
    // *** This optoin will NOT be available to OOT clients.
    std::vector<std::string> client_to_ui_services;

    // Clockwise display rotation, in degrees.
    int display_rotation = 0;

    bool use_flatland = false;

    float display_pixel_density = 0;
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
  //
  // `use_scene_provider` indicates whether UITestManager should use
  // `fuchsia.ui.test.scene.Provider` to initialize the scene, or use the raw
  // root presenter / scene manager APIs.
  //
  // TODO(fxbug.dev/103985): Remove the raw API option once web-semantics-test
  // can use `fuchsia.ui.test.scene.Provider` without flaking.
  void InitializeScene(bool use_scene_provider = true);

  // Returns the view ref koid of the client view if it's available, and false
  // otherwise.
  //
  // NOTE: Different scene owners have different policies about client view
  // refs, so users should NOT use this method as a proxy for determining that
  // the client view is attached to the scene. Use |ClientViewIsRendering| for
  // that purpose.
  std::optional<zx_koid_t> ClientViewRefKoid();

  // Convenience method to inform the client if its view is rendering.
  // Syntactic sugar for `ViewIsRendering(ClientViewRefKoid())`.
  //
  // Returns true if the client's view ref koid is present in the most recent
  // view tree snapshot received from scenic.
  bool ClientViewIsRendering();

  // Convenience method to inform the client if its view is focused.
  bool ClientViewIsFocused();

  // Convenience method to inform the client of its view scale factor.
  //
  // Returns the scale factor applied to the client view, as reported in the
  // Layout information received from the geometry observer.
  float ClientViewScaleFactor();

  // Convenience method to inform the client if the view specified by
  // `view_ref_koid` is rendering content.
  //
  // Returns true if `view_ref_koid` is present in the most recent view tree
  // snapshot received from scenic.
  bool ViewIsRendering(zx_koid_t view_ref_koid);

 private:
  // Helper methods to configure the test realm.
  void ConfigureClientSubrealm();
  void ConfigureAccessibility();
  void ConfigureSceneProvider();
  void RouteConfigData();

  // Helper method to route a set of services from the specified source to the
  // spceified targets.
  void RouteServices(std::vector<std::string> services, component_testing::Ref source,
                     std::vector<component_testing::Ref> targets);

  // Helper method to monitor the state of the view tree continuously.
  void WatchViewTree();

  // |fuchsia::ui::focus::FocusChainListener|
  void OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                     OnFocusChangeCallback callback) override;

  // Helper method to determine the component url used to instantiate the base
  // UI realm.
  std::string CalculateBaseRealmUrl();

  Config config_;
  component_testing::RealmBuilder realm_builder_ =
      component_testing::RealmBuilder::CreateFromRelativeUrl(CalculateBaseRealmUrl());
  std::shared_ptr<component_testing::RealmRoot> realm_root_;
  fuchsia::ui::observation::test::RegistrySyncPtr observer_registry_;
  fuchsia::ui::observation::geometry::ProviderPtr geometry_provider_;
  fidl::Binding<fuchsia::ui::focus::FocusChainListener> focus_chain_listener_binding_;
  fuchsia::ui::test::scene::ProviderPtr scene_provider_;

  // Connection to scene owner service. At most one will be active for a given
  // UITestManager instance.
  fuchsia::session::scene::ManagerPtr scene_manager_;
  fuchsia::ui::policy::PresenterPtr root_presenter_;

  std::optional<zx_koid_t> client_view_ref_koid_ = std::nullopt;

  // Holds the most recent view tree snapshot received from the geometry
  // observer.
  //
  // From this snapshot, we can retrieve relevant view tree state on demand,
  // e.g. if the client view is rendering content.
  std::optional<fuchsia::ui::observation::geometry::ViewTreeSnapshot> last_view_tree_snapshot_;

  // Holds the most recent focus chain received from the geometry observer.
  std::optional<fuchsia::ui::focus::FocusChain> last_focus_chain_;

  // Some tests may not need a dedicated subrealm. Those clients will not call
  // AddSubrealm(), so UITestManager will crash if it tries to add routes
  // to/from the missing subrealm.
  //
  // NOTE: This piece of state is temporary, and can be removed once the client
  // owns a full RealmBuilder instance, as opposed to a child realm.
  bool has_client_subrealm_ = false;

  // Add state as necessary.

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(UITestManager);
};

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UI_TEST_MANAGER_UI_TEST_MANAGER_H_
