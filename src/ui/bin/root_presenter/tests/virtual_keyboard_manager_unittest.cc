// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

namespace root_presenter {
namespace virtual_keyboard_manager {
namespace {

class FakeVirtualKeyboardCoordinator : public VirtualKeyboardCoordinator {
 public:
  FakeVirtualKeyboardCoordinator() : weak_ptr_factory_(this) {}
  virtual ~FakeVirtualKeyboardCoordinator() = default;

  // |VirtualKeyboardCoordinator|
  void NotifyVisibilityChange(
      bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason) override {
    is_visible_ = is_visible;
    change_reason_ = reason;
  }
  void NotifyManagerError(zx_status_t error) override { manager_error_ = error; }
  void RequestTypeAndVisibility(zx_koid_t requestor_view_koid,
                                fuchsia::input::virtualkeyboard::TextType text_type,
                                bool is_visibile) override {
    FX_NOTIMPLEMENTED();
  }
  void NotifyFocusChange(fuchsia::ui::views::ViewRef focused_view) override { FX_NOTIMPLEMENTED(); }

  // Test support.
  const auto& is_visible() { return is_visible_; };
  const auto& change_reason() { return change_reason_; }
  const auto& manager_error() { return manager_error_; }

  fxl::WeakPtr<FakeVirtualKeyboardCoordinator> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  std::optional<bool> is_visible_;
  std::optional<fuchsia::input::virtualkeyboard::VisibilityChangeReason> change_reason_;
  std::optional<zx_status_t> manager_error_;
  fxl::WeakPtrFactory<FakeVirtualKeyboardCoordinator> weak_ptr_factory_;
};

class VirtualKeyboardManagerTest : public gtest::TestLoopFixture {
 protected:
  VirtualKeyboardManagerTest() {}
  auto* context_provider() { return &context_provider_; }
  fxl::WeakPtr<FakeVirtualKeyboardCoordinator> coordinator() { return coordinator_.GetWeakPtr(); }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  FakeVirtualKeyboardCoordinator coordinator_;
};

TEST_F(VirtualKeyboardManagerTest, CtorDoesNotCrash) {
  VirtualKeyboardManager(coordinator(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
}

TEST_F(VirtualKeyboardManagerTest, FirstWatchReturnsImmediately) {
  bool was_called = false;
  VirtualKeyboardManager(coordinator(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .WatchTypeAndVisibility([&was_called](fuchsia::input::virtualkeyboard::TextType text_type,
                                            bool is_visible) { was_called = true; });
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardManagerTest, InitialVisibilityIsFalse) {
  std::optional<bool> is_visible;
  VirtualKeyboardManager(coordinator(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType text_type,
                                  bool is_vis) { is_visible = is_vis; });
  ASSERT_EQ(false, is_visible);
}

TEST_F(VirtualKeyboardManagerTest, SecondWatchHangsUntilChange) {
  VirtualKeyboardManager manager(coordinator(),
                                 fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

  // Make the initial call to WatchTypeAndVisibility(), which invokes its callback immediately, so
  // that the next call will block until type or visibility changes.
  manager.WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {});

  // Invoke WatchTypeAndVisibility() again without changing either parameter. This call should
  // _not_ invoke its callback yet.
  bool was_called = false;
  manager.WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType text_type,
                                     bool is_visible) { was_called = true; });
  ASSERT_FALSE(was_called);

  // Make a no-op request. WatchTypeAndVisibility() should _not_ invoke its callback yet.
  manager.OnTypeOrVisibilityChange(fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC, false);
  ASSERT_FALSE(was_called);

  // Change visibility. Now, Manager should invoke its callback.
  manager.OnTypeOrVisibilityChange(fuchsia::input::virtualkeyboard::TextType::PHONE, false);
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardManagerTest, SecondWatchReturnsImmediatelyIfAlreadyChanged) {
  VirtualKeyboardManager manager(coordinator(),
                                 fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);

  // Make the initial call to WatchTypeAndVisibility(), which invokes its callback immediately, so
  // that we know we're exercising the second-and-later logic below.
  manager.WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType text_type, bool is_visibile) {});

  // Make a change before invoking WatchTypeAndVisibility() again.
  manager.OnTypeOrVisibilityChange(fuchsia::input::virtualkeyboard::TextType::PHONE, true);

  // Invoke WatchTypeAndVisibility() again. The callback should be invoked immediately.
  bool was_called = false;
  manager.WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType text_type,
                                     bool is_visibile) { was_called = true; });
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardManagerTest, FirstWatchCallbackIsOnlyInvokedOnce) {
  // Make the initial call to WatchTypeAndVisibility(), which invokes its callback immediately.
  size_t n_calls = 0;
  VirtualKeyboardManager manager(coordinator(),
                                 fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  manager.WatchTypeAndVisibility([&n_calls](fuchsia::input::virtualkeyboard::TextType text_type,
                                            bool is_visible) { ++n_calls; });
  ASSERT_EQ(1u, n_calls);

  manager.OnTypeOrVisibilityChange(fuchsia::input::virtualkeyboard::TextType::PHONE, false);
  ASSERT_EQ(1u, n_calls);
}

TEST_F(VirtualKeyboardManagerTest, SecondWatchCallbackIsOnlyInvokedOnce) {
  // Make the initial call to WatchTypeAndVisibility(), which invokes its callback immediately,
  // so that we know we're exercising the second-and-later logic below.
  VirtualKeyboardManager manager(coordinator(),
                                 fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  manager.WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {});

  // Set a watch, and make a change, causing the watch to fire.
  size_t n_callbacks = 0;
  manager.WatchTypeAndVisibility(
      [&](fuchsia::input::virtualkeyboard::TextType text_type, bool visibility) { ++n_callbacks; });
  manager.OnTypeOrVisibilityChange(fuchsia::input::virtualkeyboard::TextType::PHONE, false);
  ASSERT_EQ(1u, n_callbacks);

  // Watches are one-shot, so another should _not_ trigger another callback.
  manager.OnTypeOrVisibilityChange(fuchsia::input::virtualkeyboard::TextType::PHONE, true);
  ASSERT_EQ(1u, n_callbacks);
}

TEST_F(VirtualKeyboardManagerTest, ConcurrentWatchesReportErrorToCoordinator) {
  // Make the initial call to WatchTypeAndVisibility(), which invokes its callback immediately,
  // so that we know we're exercising the second-and-later logic below.
  VirtualKeyboardManager manager(coordinator(),
                                 fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  manager.WatchTypeAndVisibility(
      [](fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {});

  // Set up first watch.
  bool was_first_callback_called = false;
  manager.WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType text_type,
                                     bool visibility) { was_first_callback_called = true; });

  // Set up second watch.
  bool was_second_callback_called = false;
  manager.WatchTypeAndVisibility([&](fuchsia::input::virtualkeyboard::TextType text_type,
                                     bool visibility) { was_second_callback_called = true; });

  ASSERT_EQ(ZX_ERR_BAD_STATE, coordinator()->manager_error());
}

class VirtualKeyboardManagerWatchTestFixture
    : public VirtualKeyboardManagerTest,
      public testing::WithParamInterface<
          std::tuple<fuchsia::input::virtualkeyboard::TextType, bool>> {};

TEST_P(VirtualKeyboardManagerWatchTestFixture, WatchProvidesCorrectValues) {
  const auto& [expected_text_type, expected_visibility] = GetParam();
  std::optional<fuchsia::input::virtualkeyboard::TextType> actual_text_type;
  std::optional<bool> actual_visibility;
  VirtualKeyboardManager manager(coordinator(),
                                 fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  manager.OnTypeOrVisibilityChange(expected_text_type, expected_visibility);
  manager.WatchTypeAndVisibility(
      [&](fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {
        actual_text_type = text_type;
        actual_visibility = is_visible;
      });
  EXPECT_EQ(expected_text_type, actual_text_type);
  EXPECT_EQ(expected_visibility, actual_visibility);
}

INSTANTIATE_TEST_SUITE_P(
    VirtualKeyboardManagerWatchTests, VirtualKeyboardManagerWatchTestFixture,
    ::testing::Combine(::testing::Values(fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC,
                                         fuchsia::input::virtualkeyboard::TextType::NUMERIC,
                                         fuchsia::input::virtualkeyboard::TextType::PHONE),
                       ::testing::Values(false, true)));

TEST_F(VirtualKeyboardManagerTest, NotifyInvokesCallback) {
  bool was_called = false;
  VirtualKeyboardManager(coordinator(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
              [&was_called]() { was_called = true; });
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardManagerTest, NotifyInvokesCallbackEvenIfCoordinatorIsNull) {
  bool was_called = false;
  std::optional<FakeVirtualKeyboardCoordinator> coordinator(std::in_place);
  VirtualKeyboardManager manager(coordinator->GetWeakPtr(),
                                 fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
  coordinator.reset();
  manager.Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                 [&was_called]() { was_called = true; });
  ASSERT_TRUE(was_called);
}

class VirtualKeyboardManagerNotifyTestFixture
    : public VirtualKeyboardManagerTest,
      public testing::WithParamInterface<
          std::tuple<bool, fuchsia::input::virtualkeyboard::VisibilityChangeReason>> {};

TEST_P(VirtualKeyboardManagerNotifyTestFixture, InformsCoordinator) {
  const auto& [expected_visibility, expected_reason] = GetParam();
  VirtualKeyboardManager(coordinator(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .Notify(expected_visibility, expected_reason, []() {});
  EXPECT_EQ(expected_visibility, coordinator()->is_visible());
  EXPECT_EQ(expected_reason, coordinator()->change_reason());
}

INSTANTIATE_TEST_SUITE_P(
    VirtualKeyboardManagerNotifyTests, VirtualKeyboardManagerNotifyTestFixture,
    ::testing::Combine(
        ::testing::Values(false, true),
        ::testing::Values(fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
                          fuchsia::input::virtualkeyboard::VisibilityChangeReason::PROGRAMMATIC)));
}  // namespace
}  // namespace virtual_keyboard_manager
}  // namespace root_presenter
