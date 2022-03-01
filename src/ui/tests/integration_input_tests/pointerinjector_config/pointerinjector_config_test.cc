// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/ui/accessibility/view/cpp/fidl.h>
#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/configuration/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/magnifier/tests/mocks/mock_magnifier.h"

constexpr auto kRootPresenter = "root_presenter";
constexpr auto kScenicTestRealm = "scenic-test-realm";
constexpr auto kMockMagnifier = "mock_magnifier";

// Types imported for the realm_builder library.
using component_testing::ChildRef;
using component_testing::LocalComponent;
using component_testing::LocalComponentHandles;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmRoot;
using component_testing::Route;
using RealmBuilder = component_testing::RealmBuilder;

constexpr zx::duration kTimeout = zx::min(5);

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  zx_info_handle_basic_t info{};
  if (view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) !=
      ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

class MockMagnifierImpl : public accessibility_test::MockMagnifier, public LocalComponent {
 public:
  explicit MockMagnifierImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  // |fuchsia::accessibility::Magnifier|
  void RegisterHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::MagnificationHandler> handler) override {
    handler_ = handler.Bind();
  }

  // |MockComponent::Start|
  // When the component framework requests for this component to start, this
  // method will be invoked by the realm_builder library.
  void Start(std::unique_ptr<LocalComponentHandles> mock_handles) override {
    // When this component starts, add a binding to the fuchsia.accessibility.Magnifier
    // protocol to this component's outgoing directory.
    FX_CHECK(
        mock_handles->outgoing()->AddPublicService(
            fidl::InterfaceRequestHandler<fuchsia::accessibility::Magnifier>([this](auto request) {
              bindings_.AddBinding(this, std::move(request), dispatcher_);
            })) == ZX_OK);
    mock_handles_ = std::move(mock_handles);
  }

  fuchsia::accessibility::MagnificationHandler* handler() { return handler_.get(); }

  bool IsBound() { return handler_.is_bound(); }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  std::unique_ptr<LocalComponentHandles> mock_handles_;
  fidl::BindingSet<fuchsia::accessibility::Magnifier> bindings_;
  fuchsia::accessibility::MagnificationHandlerPtr handler_;
};

class PointerInjectorConfigTest : public gtest::RealLoopFixture {
 protected:
  explicit PointerInjectorConfigTest() : realm_builder_(RealmBuilder::Create()) {}
  ~PointerInjectorConfigTest() override {}

  void SetUp() override {
    // Post a "just in case" quit task, if the test hangs.
    async::PostDelayedTask(
        dispatcher(),
        [] { FX_LOGS(FATAL) << "\n\n>> Test did not complete in time, terminating.  <<\n\n"; },
        kTimeout);

    // Add static test realm as a component to the realm.
    realm_builder_.AddLegacyChild(
        kRootPresenter,
        "fuchsia-pkg://fuchsia.com/pointerinjector-config-test#meta/root_presenter.cmx");
    realm_builder_.AddChild(
        kScenicTestRealm,
        "fuchsia-pkg://fuchsia.com/pointerinjector-config-test#meta/scenic-test-realm.cm");

    // Setup MockMagnifier for Root Presenter to connect to.
    mock_magnifier_ = std::make_unique<MockMagnifierImpl>(dispatcher());
    realm_builder_.AddLocalChild(kMockMagnifier, mock_magnifier());

    // Capabilities routed from test_manager to components in realm.
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                               Protocol{fuchsia::vulkan::loader::Loader::Name_},
                               Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                               Protocol{fuchsia::sysmem::Allocator::Name_},
                               Protocol{fuchsia::tracing::provider::Registry::Name_}},
              .source = ParentRef(),
              .targets = {ChildRef{kScenicTestRealm}}});
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::tracing::provider::Registry::Name_}},
              .source = ParentRef(),
              .targets = {ChildRef{kRootPresenter}}});

    // Capabilities routed between siblings in realm.
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::accessibility::Magnifier::Name_}},
              .source = ChildRef{kMockMagnifier},
              .targets = {ChildRef{kRootPresenter}}});
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::scenic::Scenic::Name_},
                               Protocol{fuchsia::ui::focus::FocusChainListenerRegistry::Name_}},
              .source = ChildRef{kScenicTestRealm},
              .targets = {ChildRef{kRootPresenter}}});

    // Capabilities routed up to test driver (this component).
    realm_builder_.AddRoute(
        Route{.capabilities = {Protocol{fuchsia::ui::pointerinjector::configuration::Setup::Name_},
                               Protocol{fuchsia::ui::accessibility::view::Registry::Name_}},
              .source = ChildRef{kRootPresenter},
              .targets = {ParentRef()}});

    // Finally, build the realm using the provided components and routes.
    realm_ = std::make_unique<RealmRoot>(realm_builder_.Build());
  }

  RealmRoot* realm() { return realm_.get(); }
  MockMagnifierImpl* mock_magnifier() { return mock_magnifier_.get(); }

 private:
  RealmBuilder realm_builder_;
  std::unique_ptr<RealmRoot> realm_;
  std::unique_ptr<MockMagnifierImpl> mock_magnifier_;
};

// Checks that GetViewRefs() returns the same ViewRefs after a11y registers a view.
TEST_F(PointerInjectorConfigTest, GetViewRefs) {
  // Connect to pointerinjector::configuration::Setup.
  auto config_setup = realm()->Connect<fuchsia::ui::pointerinjector::configuration::Setup>();

  // Get ViewRefs before a11y sets up.
  fuchsia::ui::views::ViewRef first_context;
  fuchsia::ui::views::ViewRef first_target;
  config_setup->GetViewRefs(
      [&](fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
        first_context = std::move(context);
        first_target = std::move(target);
      });

  // Create view token and view ref pairs for a11y view.
  auto [a11y_view_token, a11y_view_holder_token] = scenic::ViewTokenPair::New();
  auto [a11y_control_ref, a11y_view_ref] = scenic::ViewRefPair::New();
  auto a11y = realm()->Connect<fuchsia::ui::accessibility::view::Registry>();
  a11y->CreateAccessibilityViewHolder(std::move(a11y_view_ref), std::move(a11y_view_holder_token),
                                      [](fuchsia::ui::views::ViewHolderToken) {});

  // Get ViewRefs after a11y is set up.
  bool checked_view_refs = false;
  config_setup->GetViewRefs(
      [&](fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) {
        // Check that the ViewRefs match
        EXPECT_EQ(ExtractKoid(context), ExtractKoid(first_context));
        EXPECT_EQ(ExtractKoid(target), ExtractKoid(first_target));
        checked_view_refs = true;
      });

  RunLoopUntil([&] { return checked_view_refs; });
}

TEST_F(PointerInjectorConfigTest, WatchViewport) {
  // Connect to pointerinjector::configuration::Setup.
  auto config_setup = realm()->Connect<fuchsia::ui::pointerinjector::configuration::Setup>();

  // Get the starting viewport.
  fuchsia::ui::pointerinjector::Viewport starting_viewport, updated_viewport;
  bool watch_viewport_returned = false;
  config_setup->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport viewport) {
    starting_viewport = std::move(viewport);
    watch_viewport_returned = true;
  });

  RunLoopUntil([this, &watch_viewport_returned] {
    return mock_magnifier()->IsBound() && watch_viewport_returned;
  });

  // Queue another call to WatchViewport().
  bool viewport_updated = false;
  config_setup->WatchViewport([&](fuchsia::ui::pointerinjector::Viewport viewport) {
    updated_viewport = std::move(viewport);
    viewport_updated = true;
  });

  // Trigger a viewport update and assert that the queued WatchViewport() returns.
  mock_magnifier()->handler()->SetClipSpaceTransform(100, 100, 100, []() {});
  RunLoopUntil([&] { return viewport_updated; });

  EXPECT_NE(updated_viewport.viewport_to_context_transform(),
            starting_viewport.viewport_to_context_transform());
}
