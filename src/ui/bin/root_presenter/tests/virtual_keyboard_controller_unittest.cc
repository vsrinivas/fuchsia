// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_controller.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace root_presenter {
namespace virtual_keyboard_controller {
namespace {

class VirtualKeyboardControllerTest : public gtest::TestLoopFixture {
 protected:
  VirtualKeyboardControllerTest() { view_ref_pair_ = scenic::ViewRefPair::New(); }

  fuchsia::ui::views::ViewRef view_ref() const {
    fuchsia::ui::views::ViewRef clone;
    EXPECT_EQ(ZX_OK, view_ref_pair_.view_ref.Clone(&clone));
    return clone;
  }

 private:
  scenic::ViewRefPair view_ref_pair_;
};

TEST_F(VirtualKeyboardControllerTest, FirstWatchReturnsImmediately) {
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  bool was_called = false;
  controller.WatchVisibility([&was_called](bool visibility) { was_called = true; });
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardControllerTest, InitialVisibilityIsFalse) {
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  std::optional<bool> is_visible;
  controller.WatchVisibility([&is_visible](bool visibility) { is_visible = visibility; });
  ASSERT_EQ(false, is_visible);
}

TEST_F(VirtualKeyboardControllerTest, SecondWatchHangsUntilChange) {
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

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
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

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
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
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
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
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
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

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
  auto controller = VirtualKeyboardController(
      view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

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

TEST_F(VirtualKeyboardControllerTest, SetTextTypeDoesNotCrash) {
  VirtualKeyboardController(view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .SetTextType(fuchsia::input::virtualkeyboard::TextType::NUMERIC);
}

}  // namespace
}  // namespace virtual_keyboard_controller
}  // namespace root_presenter
