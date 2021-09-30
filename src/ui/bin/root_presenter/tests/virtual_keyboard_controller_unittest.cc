// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <optional>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

namespace root_presenter {
namespace virtual_keyboard_controller {
namespace {

class FakeVirtualKeyboardCoordinator : public VirtualKeyboardCoordinator {
 public:
  FakeVirtualKeyboardCoordinator() : weak_ptr_factory_(this) {}
  virtual ~FakeVirtualKeyboardCoordinator() = default;

  // |VirtualKeyboardCoordinator|
  void NotifyVisibilityChange(
      bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason) override {
    FX_NOTIMPLEMENTED();
  }
  void NotifyManagerError(zx_status_t error) override { FX_NOTIMPLEMENTED(); }
  void RequestTypeAndVisibility(fuchsia::input::virtualkeyboard::TextType text_type,
                                bool is_visible) override {
    requested_text_type_ = text_type;
    want_visible_ = is_visible;
  }
  void NotifyFocusChange(fuchsia::ui::views::ViewRef focused_view) override { FX_NOTIMPLEMENTED(); }

  // Test support.
  void Reset() {
    want_visible_.reset();
    requested_text_type_.reset();
  }
  const auto& want_visible() { return want_visible_; }
  const auto& requested_text_type() { return requested_text_type_; }

  fxl::WeakPtr<FakeVirtualKeyboardCoordinator> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  std::optional<bool> want_visible_;
  std::optional<fuchsia::input::virtualkeyboard::TextType> requested_text_type_;
  fxl::WeakPtrFactory<FakeVirtualKeyboardCoordinator> weak_ptr_factory_;
};

class VirtualKeyboardControllerTest : public gtest::TestLoopFixture {
 protected:
  VirtualKeyboardControllerTest() { view_ref_pair_ = scenic::ViewRefPair::New(); }

  fuchsia::ui::views::ViewRef view_ref() const {
    fuchsia::ui::views::ViewRef clone;
    EXPECT_EQ(ZX_OK, view_ref_pair_.view_ref.Clone(&clone));
    return clone;
  }

  fxl::WeakPtr<FakeVirtualKeyboardCoordinator> coordinator() { return coordinator_.GetWeakPtr(); }

 private:
  scenic::ViewRefPair view_ref_pair_;
  FakeVirtualKeyboardCoordinator coordinator_;
};

TEST_F(VirtualKeyboardControllerTest, FirstWatchReturnsImmediately) {
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  bool was_called = false;
  controller.WatchVisibility([&was_called](bool visibility) { was_called = true; });
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardControllerTest, InitialVisibilityIsFalse) {
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  std::optional<bool> is_visible;
  controller.WatchVisibility([&is_visible](bool visibility) { is_visible = visibility; });
  ASSERT_EQ(false, is_visible);
}

TEST_F(VirtualKeyboardControllerTest, SecondWatchHangsUntilChange) {
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

  // Make the initial call to WatchVisibility(), which invokes its callback immediately, so that
  // the next call will block until a visibility change.
  controller.WatchVisibility([](bool visibility) {});

  // Invoke WatchVisibility() again without any change to visibility. WatchVisibility() should _not_
  // invoke its callback.
  bool was_called = false;
  controller.WatchVisibility([&was_called](bool visibility) { was_called = true; });
  ASSERT_FALSE(was_called);

  // Make a no-op request. WatchVisibility() should _not_ invoke its callback.
  controller.RequestHide();
  ASSERT_FALSE(was_called);

  // Make a request that changes visibility. WatchVisibility() _should_ invoke its callback.
  controller.RequestShow();
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardControllerTest, SecondWatchReturnsImmediatelyIfAlreadyChanged) {
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

  // Make the initial call to WatchVisibility(), which invokes its callback immediately, so that
  // we know we're exercise the second-and-later logic.
  controller.WatchVisibility([](bool visibility) {});

  // Make a change before invoking WatchVisibility().
  controller.RequestShow();

  // Invoke WatchVisibility() again. The callback should be invoked immediately.
  bool was_called = false;
  controller.WatchVisibility([&was_called](bool visibility) { was_called = true; });

  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardControllerTest, FirstWatchCallbackIsOnlyInvokedOnce) {
  // Make the initial call to WatchVisibility(), which invokes its callback immediately.
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  size_t n_callbacks = 0;
  controller.WatchVisibility([&n_callbacks](bool visibility) { ++n_callbacks; });
  ASSERT_EQ(1u, n_callbacks);

  // Watches are one-shot, so a change to visibility should not trigger another callback.
  controller.RequestShow();
  ASSERT_EQ(1u, n_callbacks);
}

TEST_F(VirtualKeyboardControllerTest, SecondWatchCallbackIsOnlyInvokedOnce) {
  // Make the initial call to WatchVisibility(), which invokes its callback immediately, so that
  // we know we're exercise the second-and-later logic.
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  controller.WatchVisibility([](bool visibility) {});

  // Set a watch, and make a change, causing the watch to fire.
  size_t n_callbacks = 0;
  controller.WatchVisibility([&n_callbacks](bool visibility) { ++n_callbacks; });
  controller.RequestShow();
  ASSERT_EQ(1u, n_callbacks);

  // Watches are one-shot, so a change to visibility should not trigger another callback.
  controller.RequestHide();
  ASSERT_EQ(1u, n_callbacks);
}

TEST_F(VirtualKeyboardControllerTest, ConcurrentCallsLastWatcherGetsNewValue) {
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

  // Make the initial call to WatchVisibility(), which invokes its callback immediately, so that
  // subsequent calls will block until a visibility change.
  controller.WatchVisibility([](bool visibility) {});

  // Invoke WatchVisibility() twice, concurrently. Then change the visibility.
  // The later call should get the new value.
  std::optional<bool> last_watcher_visibility;
  controller.WatchVisibility([](bool visibility) {});
  controller.WatchVisibility(
      [&last_watcher_visibility](bool visibility) { last_watcher_visibility = visibility; });
  controller.RequestShow();
  ASSERT_EQ(true, last_watcher_visibility);
}

TEST_F(VirtualKeyboardControllerTest, ConcurrentCallsFirstWatchersGetsOldValue) {
  auto controller = FidlBoundVirtualKeyboardController(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

  // Make the initial call to WatchVisibility(), which invokes its callback immediately, so that
  // subsequent calls will block until a visibility change.
  controller.WatchVisibility([](bool visibility) {});

  // Invoke WatchVisibility() twice, concurrently. Then change the visibility.
  // The earlier call should get the old value.
  std::optional<bool> first_watcher_visibility;
  controller.WatchVisibility(
      [&first_watcher_visibility](bool visibility) { first_watcher_visibility = visibility; });
  controller.WatchVisibility([](bool visibility) {});
  controller.RequestShow();
  ASSERT_EQ(false, first_watcher_visibility);
}

TEST_F(VirtualKeyboardControllerTest, RequestShowInformsCoordinatorOfVisibility) {
  FidlBoundVirtualKeyboardController(coordinator(), view_ref(),
                                     fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .RequestShow();
  ASSERT_EQ(true, coordinator()->want_visible());
}

TEST_F(VirtualKeyboardControllerTest, RequestHideInformsCoordinatorOfVisibility) {
  FidlBoundVirtualKeyboardController(coordinator(), view_ref(),
                                     fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .RequestHide();
  ASSERT_EQ(false, coordinator()->want_visible());
}

TEST_F(VirtualKeyboardControllerTest, RequestShowDoesNotCrashWhenCoordinatorIsNull) {
  std::optional<FakeVirtualKeyboardCoordinator> coordinator(std::in_place);
  FidlBoundVirtualKeyboardController controller(
      coordinator->GetWeakPtr(), view_ref(),
      fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  coordinator.reset();
  controller.RequestShow();
}

TEST_F(VirtualKeyboardControllerTest, RequestHideDoesNotCrashWhenCoordinatorIsNull) {
  std::optional<FakeVirtualKeyboardCoordinator> coordinator(std::in_place);
  FidlBoundVirtualKeyboardController controller(
      coordinator->GetWeakPtr(), view_ref(),
      fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  coordinator.reset();
  controller.RequestHide();
}

TEST_F(VirtualKeyboardControllerTest, SetTextTypeKeepsKeyboardShown) {
  FidlBoundVirtualKeyboardController controller(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  controller.RequestShow();
  coordinator()->Reset();
  controller.SetTextType(::fuchsia::input::virtualkeyboard::TextType::PHONE);
  ASSERT_EQ(true, coordinator()->want_visible());
}

TEST_F(VirtualKeyboardControllerTest, SetTextTypeKeepsKeyboardHidden) {
  FidlBoundVirtualKeyboardController controller(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  coordinator()->Reset();
  controller.SetTextType(::fuchsia::input::virtualkeyboard::TextType::PHONE);
  ASSERT_EQ(false, coordinator()->want_visible());
}

TEST_F(VirtualKeyboardControllerTest, SetTextTypeDoesNotReopenKeyboardClosedByUser) {
  // Create controller, and request that the keyboard be shown.
  FidlBoundVirtualKeyboardController controller(
      coordinator(), view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  controller.RequestShow();
  ASSERT_EQ(true, coordinator()->want_visible());

  // Report that the user hid the keyboard, and reset previous state of the fake coordinator.
  controller.OnUserAction(VirtualKeyboardController::UserAction::HIDE_KEYBOARD);
  coordinator()->Reset();
  ASSERT_EQ(std::nullopt, coordinator()->want_visible());

  // Modify the text type. This should not override the user's choice to hide the keyboard.
  controller.SetTextType(::fuchsia::input::virtualkeyboard::TextType::PHONE);
  ASSERT_EQ(false, coordinator()->want_visible());
}

class VirtualKeyboardControllerTextTypeParamFixture
    : public VirtualKeyboardControllerTest,
      public testing::WithParamInterface<fuchsia::input::virtualkeyboard::TextType> {};

TEST_P(VirtualKeyboardControllerTextTypeParamFixture,
       RequestShowInformsCoordinatorOfInitialTextType) {
  auto expected_text_type = GetParam();
  FidlBoundVirtualKeyboardController(coordinator(), view_ref(), expected_text_type).RequestShow();
  ASSERT_EQ(expected_text_type, coordinator()->requested_text_type());
}

TEST_P(VirtualKeyboardControllerTextTypeParamFixture, SetTextTypeInformsCoordinator) {
  auto expected_text_type = GetParam();
  FidlBoundVirtualKeyboardController(coordinator(), view_ref(),
                                     fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .SetTextType(expected_text_type);
  ASSERT_EQ(expected_text_type, coordinator()->requested_text_type());
}

INSTANTIATE_TEST_SUITE_P(VirtualKeyboardControllerTextTypeParameterizedTests,
                         VirtualKeyboardControllerTextTypeParamFixture,
                         ::testing::Values(fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC,
                                           fuchsia::input::virtualkeyboard::TextType::NUMERIC,
                                           fuchsia::input::virtualkeyboard::TextType::PHONE));

TEST_F(VirtualKeyboardControllerTest, SetTextTypeDoesNotCrashWhenCoordinatorIsNull) {
  std::optional<FakeVirtualKeyboardCoordinator> coordinator(std::in_place);
  FidlBoundVirtualKeyboardController controller(
      coordinator->GetWeakPtr(), view_ref(),
      fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  coordinator.reset();
  controller.SetTextType(fuchsia::input::virtualkeyboard::TextType::NUMERIC);
}

}  // namespace
}  // namespace virtual_keyboard_controller
}  // namespace root_presenter
