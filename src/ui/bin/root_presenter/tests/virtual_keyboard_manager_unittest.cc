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
  void RequestTypeAndVisibility(fuchsia::input::virtualkeyboard::TextType text_type,
                                bool is_visibile) override {
    FX_NOTIMPLEMENTED();
  }

  // Test support.
  const auto& is_visible() { return is_visible_; };
  const auto& change_reason() { return change_reason_; }

  fxl::WeakPtr<FakeVirtualKeyboardCoordinator> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  std::optional<bool> is_visible_;
  std::optional<fuchsia::input::virtualkeyboard::VisibilityChangeReason> change_reason_;
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
  VirtualKeyboardManager(coordinator(), context_provider()->context());
}

TEST_F(VirtualKeyboardManagerTest, WatchTypeAndVisibilityDoesNotCrash) {
  VirtualKeyboardManager(coordinator(), context_provider()->context())
      .WatchTypeAndVisibility(
          [](fuchsia::input::virtualkeyboard::TextType text_type, bool is_visible) {});
}

TEST_F(VirtualKeyboardManagerTest, NotifyInvokesCallback) {
  bool was_called = false;
  VirtualKeyboardManager(coordinator(), context_provider()->context())
      .Notify(true, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION,
              [&was_called]() { was_called = true; });
  ASSERT_TRUE(was_called);
}

TEST_F(VirtualKeyboardManagerTest, NotifyInvokesCallbackEvenIfCoordinatorIsNull) {
  bool was_called = false;
  std::optional<FakeVirtualKeyboardCoordinator> coordinator(std::in_place);
  VirtualKeyboardManager manager(coordinator->GetWeakPtr(), context_provider()->context());
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
  VirtualKeyboardManager(coordinator(), context_provider()->context())
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
