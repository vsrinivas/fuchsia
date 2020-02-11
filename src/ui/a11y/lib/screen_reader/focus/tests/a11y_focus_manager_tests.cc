// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/real_loop_fixture.h>

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_focuser.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

namespace {
class ViewRefHelper {
 public:
  ViewRefHelper() {
    FX_CHECK(zx::eventpair::create(0u, &view_ref_.reference, &eventpair_peer_) == ZX_OK);
  }

  // Helper function for sending reset signal to the view_ref_.
  void SendEventPairSignal() { eventpair_peer_.reset(); }

  fuchsia::ui::views::ViewRef GetViewRef() const { return a11y::Clone(view_ref_); }

 private:
  fuchsia::ui::views::ViewRef view_ref_;

  // The event signaling pair member, used to invalidate the View Ref.
  zx::eventpair eventpair_peer_;
};

}  // namespace

class A11yFocusManagerTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    auto focuser_handle = focuser_bindings_.AddBinding(&focuser_);
    a11y_focus_manager_ = std::make_unique<a11y::A11yFocusManager>(focuser_handle.Bind());
    syslog::InitLogger();
  }

  // Helper function to check if the given ViewRef has a11y focus.
  void CheckViewInFocus(const ViewRefHelper& view_ref_helper, uint32_t node_id) const {
    auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
    ASSERT_TRUE(a11y_focus.has_value());
    EXPECT_EQ(a11y::GetKoid(view_ref_helper.GetViewRef()), a11y_focus.value().view_ref_koid);
    EXPECT_EQ(node_id, a11y_focus.value().node_id);
  }

  // Helper function to check if the given ViewRef does not have a11y focus.
  void CheckViewNotInFocus(const ViewRefHelper& view_ref_helper) const {
    auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
    ASSERT_TRUE(a11y_focus.has_value());
    EXPECT_NE(a11y::GetKoid(view_ref_helper.GetViewRef()), a11y_focus.value().view_ref_koid);
  }

  MockFocuser focuser_;
  std::unique_ptr<a11y::A11yFocusManager> a11y_focus_manager_;
  fidl::BindingSet<fuchsia::ui::views::Focuser> focuser_bindings_;
};

// GetA11yFocus() doesn't return anything when no view is in focus.
TEST_F(A11yFocusManagerTest, GetA11yFocusNoViewFound) {
  // By default no view is in a11y focus.
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus.has_value());
}

// Setting a11y focus to a view for the first time adds view to the map, defaults focused
// node to root node and sets the current view to be in focus.
TEST_F(A11yFocusManagerTest, AddViewRefForNewView) {
  ViewRefHelper view_ref_helper;

  // Call AddViewRef for view_ref.
  a11y_focus_manager_->AddViewRef(view_ref_helper.GetViewRef());

  // A11y focus should be set to the newly received view_ref.
  CheckViewInFocus(view_ref_helper, a11y::A11yFocusManager::kRootNodeId);
}

// Setting a11y focus to a view which already exists, changes a11y focus to the passed view.
TEST_F(A11yFocusManagerTest, AddViewRefForExistingView) {
  ViewRefHelper view_ref_helper_1, view_ref_helper_2;

  // Add view_ref_1 by calling AddViewRef(). This should set focus to view_ref_1.
  a11y_focus_manager_->AddViewRef(view_ref_helper_1.GetViewRef());
  // Add view_ref_2 by calling AddViewRef(). This should set focus to view_ref_2.
  a11y_focus_manager_->AddViewRef(view_ref_helper_2.GetViewRef());

  // Change focus to view_ref_1.
  a11y_focus_manager_->AddViewRef(view_ref_helper_1.GetViewRef());

  // A11y focus should be set to view_ref_1.
  CheckViewInFocus(view_ref_helper_1, a11y::A11yFocusManager::kRootNodeId);
}

// SettingsA11yFocus() for a view which doesn't exist, returns false in the callback.
TEST_F(A11yFocusManagerTest, SetA11yFocusViewNotExist) {
  ViewRefHelper view_ref_helper;

  uint32_t view_1_focused_node = 5;
  bool set_focus_status = true;
  a11y_focus_manager_->SetA11yFocus(
      a11y::GetKoid(view_ref_helper.GetViewRef()), view_1_focused_node,
      [&set_focus_status](bool status) { set_focus_status = status; });
  RunLoopUntilIdle();

  EXPECT_FALSE(set_focus_status);
}

// SettingA11yFocus() to a view which is already in focus results in a call to
// RequestFocus().
TEST_F(A11yFocusManagerTest, SetA11yFocusRequestViewAlreadyInFocus) {
  ViewRefHelper view_ref_helper;
  // Call AddViewRef for view_ref.
  a11y_focus_manager_->AddViewRef(view_ref_helper.GetViewRef());

  // Call SetA11yFocus on the view which is already in focus.
  uint32_t view_1_focused_node = 5;
  bool set_focus_status = false;
  a11y_focus_manager_->SetA11yFocus(
      a11y::GetKoid(view_ref_helper.GetViewRef()), view_1_focused_node,
      [&set_focus_status](bool status) { set_focus_status = status; });
  RunLoopUntilIdle();

  // SetA11yFocus() should be successful.
  EXPECT_TRUE(set_focus_status);
  // Check RequestFocus() is not called.
  EXPECT_FALSE(focuser_.GetFocusRequestReceived());
}

// SettingA11yFocus() to a view which is not in focus results in a call to
// RequestFocus() and changes a11y focus to the new view.
TEST_F(A11yFocusManagerTest, SetA11yFocusRequestFocusViewNotInFocus) {
  ViewRefHelper view_ref_helper_1, view_ref_helper_2;

  // Call AddViewRef for view_ref_1.
  a11y_focus_manager_->AddViewRef(view_ref_helper_1.GetViewRef());
  // Call AddViewRef for view_ref_2.
  a11y_focus_manager_->AddViewRef(view_ref_helper_2.GetViewRef());

  // Call SetFocus() on view_ref_1.
  uint32_t view_1_focused_node = 5;
  bool set_focus_status = false;
  a11y_focus_manager_->SetA11yFocus(
      a11y::GetKoid(view_ref_helper_1.GetViewRef()), view_1_focused_node,
      [&set_focus_status](bool status) { set_focus_status = status; });
  RunLoopUntilIdle();

  // SetA11yFocus() should return true.
  EXPECT_TRUE(set_focus_status);
  // Check RequestFocus() is called.
  EXPECT_TRUE(focuser_.GetFocusRequestReceived());
  EXPECT_EQ(focuser_.GetViewRefKoid(), a11y::GetKoid(view_ref_helper_1.GetViewRef()));
  // Check view_ref_1 is in focus.
  CheckViewInFocus(view_ref_helper_1, view_1_focused_node);
}

// SettingA11yFocus() doesn't change focused view and returns false when RequestFocus() throws an
// error.
TEST_F(A11yFocusManagerTest, RequestFocusFails) {
  ViewRefHelper view_ref_helper_1, view_ref_helper_2;

  // Call AddViewRef for view_ref_1.
  a11y_focus_manager_->AddViewRef(view_ref_helper_1.GetViewRef());
  // Call AddViewRef for view_ref_2.
  a11y_focus_manager_->AddViewRef(view_ref_helper_2.GetViewRef());

  // Set focuser to throw error.
  focuser_.SetThrowError(true);

  // Call SetFocus() on view_ref_1.
  uint32_t view_1_focused_node = 5;
  bool set_focus_status = true;
  a11y_focus_manager_->SetA11yFocus(
      a11y::GetKoid(view_ref_helper_1.GetViewRef()), view_1_focused_node,
      [&set_focus_status](bool status) { set_focus_status = status; });
  RunLoopUntilIdle();

  // SetA11yFocus() should return false.
  EXPECT_FALSE(set_focus_status);
  // Check view_ref_1 is not in focus.
  CheckViewNotInFocus(view_ref_helper_1);
}

// When view in a11y-focus is closed then no view should be in focus.
TEST_F(A11yFocusManagerTest, FocusedViewClosed) {
  ViewRefHelper view_ref_helper;

  // Call AddViewRef for view
  a11y_focus_manager_->AddViewRef(view_ref_helper.GetViewRef());

  // Send ZX_EVENTPAIR_PEER_CLOSED signal for view_ref.
  view_ref_helper.SendEventPairSignal();
  RunLoopUntilIdle();

  // No view is in a11y focus.
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus.has_value());
}

// When view which is not currently in a11y-focus is closed then future calls to SetA11yFocus() for
// that view should fail. Currently focused view should not change in this situation.
TEST_F(A11yFocusManagerTest, NonFocusedViewClosed) {
  ViewRefHelper view_ref_helper_1, view_ref_helper_2;

  // Call AddViewRef for both the views.
  a11y_focus_manager_->AddViewRef(view_ref_helper_1.GetViewRef());
  a11y_focus_manager_->AddViewRef(view_ref_helper_2.GetViewRef());

  // Send ZX_EVENTPAIR_PEER_CLOSED signal for viewref_helper_1.
  view_ref_helper_1.SendEventPairSignal();
  RunLoopUntilIdle();

  uint32_t focused_node_id = 5;
  bool set_focus_status = true;
  a11y_focus_manager_->SetA11yFocus(
      a11y::GetKoid(view_ref_helper_1.GetViewRef()), focused_node_id,
      [&set_focus_status](bool status) { set_focus_status = status; });

  // view_ref_1 should be deleted and hence SetA11yFocus() above to view_ref_1 should fail.
  EXPECT_FALSE(set_focus_status);
  // view_ref_2 should still be in a11y focus.
  CheckViewInFocus(view_ref_helper_2, a11y::A11yFocusManager::kRootNodeId);
}

}  // namespace accessibility_test
