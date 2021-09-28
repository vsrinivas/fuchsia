// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/a11y_view.h"

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>

#include <set>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/scenic_mocks.h"

namespace accessibility_test {
namespace {

class FakeAccessibilityViewRegistry : public fuchsia::ui::accessibility::view::Registry {
 public:
  FakeAccessibilityViewRegistry(fuchsia::ui::views::ViewHolderToken client_view_holder_token)
      : client_view_holder_token_(std::move(client_view_holder_token)) {}
  ~FakeAccessibilityViewRegistry() override = default;

  // |fuchsia::ui::accessibility::view::Registry|
  void CreateAccessibilityViewHolder(fuchsia::ui::views::ViewRef a11y_view_ref,
                                     fuchsia::ui::views::ViewHolderToken a11y_view_holder_token,
                                     CreateAccessibilityViewHolderCallback callback) override {
    a11y_view_ref_ = std::move(a11y_view_ref);
    callback(std::move(client_view_holder_token_));
  }

  fidl::InterfaceRequestHandler<fuchsia::ui::accessibility::view::Registry> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](
               fidl::InterfaceRequest<fuchsia::ui::accessibility::view::Registry> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  const fuchsia::ui::views::ViewRef& a11y_view_ref() { return a11y_view_ref_; }

 private:
  fuchsia::ui::views::ViewHolderToken client_view_holder_token_;
  fuchsia::ui::views::ViewRef a11y_view_ref_;
  fidl::BindingSet<fuchsia::ui::accessibility::view::Registry> bindings_;
};

class AccessibilityViewTest : public gtest::TestLoopFixture {
 public:
  AccessibilityViewTest() = default;
  ~AccessibilityViewTest() override = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    auto mock_session = std::make_unique<MockSession>();
    mock_session_ = mock_session.get();
    mock_scenic_ = std::make_unique<MockScenic>(std::move(mock_session));

    auto [client_view_token, client_view_holder_token] = scenic::ViewTokenPair::New();
    fidl::Clone(client_view_holder_token, &client_view_holder_token_);
    fake_accessibility_view_registry_ =
        std::make_unique<FakeAccessibilityViewRegistry>(std::move(client_view_holder_token));

    context_provider_.service_directory_provider()->AddService(mock_scenic_->GetHandler());
    context_provider_.service_directory_provider()->AddService(
        fake_accessibility_view_registry_->GetHandler());

    RunLoopUntilIdle();
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  MockSession* mock_session_;
  std::unique_ptr<MockScenic> mock_scenic_;
  std::unique_ptr<FakeAccessibilityViewRegistry> fake_accessibility_view_registry_;
  fuchsia::ui::views::ViewHolderToken client_view_holder_token_;
};

TEST_F(AccessibilityViewTest, TestConstruction) {
  a11y::AccessibilityView a11y_view(context_provider_.context());

  RunLoopUntilIdle();

  EXPECT_TRUE(mock_scenic_->create_session_called());

  // Verify that a11y view was created.
  const auto& views = mock_session_->views();
  EXPECT_EQ(views.size(), 1u);
  const auto a11y_view_id = views.begin()->second.id;

  // Verify that a11y view ref was passed to accessibility view registry.
  EXPECT_EQ(a11y::GetKoid(views.begin()->second.view_ref),
            a11y::GetKoid(fake_accessibility_view_registry_->a11y_view_ref()));

  // Verify that proxy view holder was created as a child of the a11y view.
  const auto& view_holders = mock_session_->view_holders();
  EXPECT_EQ(view_holders.size(), 1u);
  EXPECT_EQ(view_holders.begin()->second.parent_id, a11y_view_id);
}

TEST_F(AccessibilityViewTest, TestViewProperties) {
  a11y::AccessibilityView a11y_view(context_provider_.context());

  RunLoopUntilIdle();

  EXPECT_TRUE(mock_scenic_->create_session_called());

  // Verify that a11y view was created.
  const auto& views = mock_session_->views();
  EXPECT_EQ(views.size(), 1u);
  const auto a11y_view_id = views.begin()->second.id;

  // Verify that a11y view does not yet have bounds.
  EXPECT_FALSE(a11y_view.get_a11y_view_properties());

  // Send "view attached to scene" event for a11y view.
  mock_session_->SendViewAttachedToSceneEvent(a11y_view_id);

  RunLoopUntilIdle();

  // Verify that a11y view properties match the properties in the event.
  auto a11y_view_properties = a11y_view.get_a11y_view_properties();
  ASSERT_TRUE(a11y_view_properties);
  // Compare a field that's nonzero in MockSession::kDefaultViewProperties.
  EXPECT_EQ(a11y_view_properties->bounding_box.min.z,
            MockSession::kDefaultViewProperties.bounding_box.min.z);

  // Send "view properties changed" event for a11y view.
  auto new_view_properties = MockSession::kDefaultViewProperties;
  new_view_properties.bounding_box.min.z = 100;
  mock_session_->SendViewPropertiesChangedEvent(a11y_view_id, new_view_properties);

  RunLoopUntilIdle();

  // Verify that a11y view properties reflect the change.
  a11y_view_properties = a11y_view.get_a11y_view_properties();
  ASSERT_TRUE(a11y_view_properties);
  EXPECT_EQ(a11y_view_properties->bounding_box.min.z, new_view_properties.bounding_box.min.z);
}

TEST_F(AccessibilityViewTest, InvokesRegisteredCallbacks) {
  a11y::AccessibilityView a11y_view(context_provider_.context());

  RunLoopUntilIdle();

  bool scene_ready = false;
  bool scene_ready_2 = false;
  bool view_properties_received = false;

  a11y_view.add_scene_ready_callback([&scene_ready]() {
    scene_ready = true;
    return true;
  });

  a11y_view.add_scene_ready_callback([&scene_ready_2]() {
    scene_ready_2 = true;
    return true;
  });

  a11y_view.add_view_properties_changed_callback(
      [&view_properties_received](fuchsia::ui::gfx::ViewProperties properties) {
        view_properties_received = true;
        return true;
      });

  const auto& views = mock_session_->views();
  EXPECT_EQ(views.size(), 1u);
  const auto a11y_view_id = views.begin()->second.id;

  // Send "view attached to scene" event for a11y view.
  mock_session_->SendViewAttachedToSceneEvent(a11y_view_id);

  RunLoopUntilIdle();

  EXPECT_FALSE(scene_ready);
  EXPECT_FALSE(scene_ready_2);
  EXPECT_TRUE(view_properties_received);
  view_properties_received = false;

  // Send "view properties changed" event for a11y view.
  auto new_view_properties = MockSession::kDefaultViewProperties;
  new_view_properties.bounding_box.min.z = 100;
  mock_session_->SendViewPropertiesChangedEvent(a11y_view_id, new_view_properties);

  RunLoopUntilIdle();

  EXPECT_TRUE(view_properties_received);

  const auto& view_holders = mock_session_->view_holders();
  auto proxy_view_id = view_holders.begin()->second.id;

  mock_session_->SendViewConnectedEvent(proxy_view_id);

  RunLoopUntilIdle();

  EXPECT_TRUE(scene_ready);
  EXPECT_TRUE(scene_ready_2);
}

TEST_F(AccessibilityViewTest, Reinitialize) {
  a11y::AccessibilityView a11y_view(context_provider_.context());

  RunLoopUntilIdle();

  // Save the a11y view viewref.
  const auto& views = mock_session_->views();
  ASSERT_EQ(views.size(), 1u);
  fuchsia::ui::views::ViewRef first_a11y_view_ref;
  first_a11y_view_ref = a11y::Clone(views.begin()->second.view_ref);

  // Re-initialize a11y view.
  a11y_view.Initialize();

  RunLoopUntilIdle();

  // Verify that a11y view was re-initialized with a new viewref.
  ASSERT_EQ(views.size(), 1u);
  EXPECT_NE(a11y::GetKoid(views.begin()->second.view_ref), a11y::GetKoid(first_a11y_view_ref));
}

TEST_F(AccessibilityViewTest, TestViewHolderDisconnected) {
  a11y::AccessibilityView a11y_view(context_provider_.context());

  RunLoopUntilIdle();

  // Save the a11y view viewref.
  const auto& views = mock_session_->views();
  ASSERT_EQ(views.size(), 1u);
  fuchsia::ui::views::ViewRef first_a11y_view_ref;
  first_a11y_view_ref = a11y::Clone(views.begin()->second.view_ref);

  // Simluate events required for the view to be "initialized".
  const auto a11y_view_id = views.begin()->second.id;
  mock_session_->SendViewPropertiesChangedEvent(a11y_view_id, {});

  const auto& view_holders = mock_session_->view_holders();
  ASSERT_EQ(view_holders.size(), 1u);
  const auto proxy_view_holder_id = view_holders.begin()->second.id;
  mock_session_->SendViewConnectedEvent(proxy_view_holder_id);

  RunLoopUntilIdle();

  EXPECT_TRUE(a11y_view.is_initialized());

  // Simulate a ViewHolderDisconnected scenic event.
  mock_session_->SendViewHolderDisconnectedEvent(a11y_view_id);

  RunLoopUntilIdle();

  // Verify that a11y view was re-initialized with a new viewref.
  ASSERT_EQ(views.size(), 1u);
  EXPECT_NE(a11y::GetKoid(views.begin()->second.view_ref), a11y::GetKoid(first_a11y_view_ref));
}

TEST_F(AccessibilityViewTest, ViewHolderDisconnectedUninitializedView) {
  a11y::AccessibilityView a11y_view(context_provider_.context());

  RunLoopUntilIdle();

  // Save the a11y view viewref.
  const auto& views = mock_session_->views();
  ASSERT_EQ(views.size(), 1u);
  fuchsia::ui::views::ViewRef first_a11y_view_ref;
  first_a11y_view_ref = a11y::Clone(views.begin()->second.view_ref);

  RunLoopUntilIdle();

  EXPECT_FALSE(a11y_view.is_initialized());

  // At this point, the a11y view is not considered "initialized", because it
  // has not received its view properties and the proxy view has not been
  // connected. Send a ViewHolderDisconnectedEvent, and verify that the a11y view
  // did NOT try to reinitialize itself.
  ASSERT_EQ(views.size(), 1u);
  const auto a11y_view_id = views.begin()->second.id;
  mock_session_->SendViewHolderDisconnectedEvent(a11y_view_id);

  RunLoopUntilIdle();

  // If the a11y view tried to reinitialize itself, then it would have created a
  // new a11y view with a different ViewRef. Verify that no such attempt was
  // made.
  EXPECT_EQ(views.size(), 1u);
  EXPECT_EQ(a11y::GetKoid(views.begin()->second.view_ref), a11y::GetKoid(first_a11y_view_ref));
}

}  // namespace
}  // namespace accessibility_test
