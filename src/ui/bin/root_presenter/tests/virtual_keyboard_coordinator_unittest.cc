// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace root_presenter {
namespace virtual_keyboard_coordinator {
namespace {

class FakeVirtualKeyboardController : public VirtualKeyboardController {
 public:
  FakeVirtualKeyboardController() = default;
  ~FakeVirtualKeyboardController() override = default;

  // |fuchsia.input.virtualkeyboard.Controller|
  void SetTextType(::fuchsia::input::virtualkeyboard::TextType text_type) override {
    FX_NOTIMPLEMENTED();
  }
  void RequestShow() override { is_visible_ = true; }
  void RequestHide() override { is_visible_ = false; }
  void WatchVisibility(WatchVisibilityCallback callback) override { FX_NOTIMPLEMENTED(); }

  // |VirtualKeyboardController|
  void OnUserAction(UserAction action) override {
    switch (action) {
      case UserAction::HIDE_KEYBOARD:
        is_visible_ = false;
        break;
      case UserAction::SHOW_KEYBOARD:
        is_visible_ = true;
        break;
    }
  }

  // Test support.
  const std::optional<bool>& is_visible() { return is_visible_; }

 private:
  std::optional<bool> is_visible_;
};

class VirtualKeyboardCoordinatorTest : public gtest::TestLoopFixture {
 protected:
  auto* context_provider() { return &context_provider_; }

 private:
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(VirtualKeyboardCoordinatorTest, CtorDoesNotCrash) {
  FidlBoundVirtualKeyboardCoordinator coordinator(context_provider()->context());
}

// For tests exercising `Create()`, see virtual_keyboard_fidl_test.cc.

// For tests observing NotifyVisibilityChange()'s side-effects over FIDL,
// see virtual_keyboard_fidl_test.cc.

TEST_F(VirtualKeyboardCoordinatorTest, NotifyVisibilityChangeDoesNotCrashWhenControllerIsNotBound) {
  FidlBoundVirtualKeyboardCoordinator coordinator(context_provider()->context());
  coordinator.NotifyVisibilityChange(
      false, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION);
}

TEST_F(VirtualKeyboardCoordinatorTest, NotifyVisibilityChangePropagatesUserChanges) {
  FidlBoundVirtualKeyboardCoordinator coordinator(context_provider()->context());
  auto controller = std::make_unique<FakeVirtualKeyboardController>();
  FakeVirtualKeyboardController* controller_ptr = controller.get();
  coordinator.SetControllerForTest(std::move(controller));
  coordinator.NotifyVisibilityChange(
      false, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION);
  ASSERT_EQ(false, controller_ptr->is_visible());
}

TEST_F(VirtualKeyboardCoordinatorTest, NotifyVisibilityChangeDoesNotPropagateProgrammaticChanges) {
  FidlBoundVirtualKeyboardCoordinator coordinator(context_provider()->context());
  auto controller = std::make_unique<FakeVirtualKeyboardController>();
  FakeVirtualKeyboardController* controller_ptr = controller.get();
  coordinator.SetControllerForTest(std::move(controller));
  coordinator.NotifyVisibilityChange(
      false, fuchsia::input::virtualkeyboard::VisibilityChangeReason::PROGRAMMATIC);
  ASSERT_EQ(std::nullopt, controller_ptr->is_visible());
}

// For tests observing RequestTypeAndVisibility()'s side-effects over FIDL,
// see virtual_keyboard_fidl_test.cc.

TEST_F(VirtualKeyboardCoordinatorTest, RequestTypeAndVisibilityDoesNotCrash) {
  FidlBoundVirtualKeyboardCoordinator coordinator(context_provider()->context());
  scenic::ViewRefPair view_ref_pair = scenic::ViewRefPair::New();
  zx_info_handle_basic_t view_ref_info{};
  ASSERT_EQ(ZX_OK,
            view_ref_pair.view_ref.reference.get_info(ZX_INFO_HANDLE_BASIC, &view_ref_info,
                                                      sizeof(view_ref_info), nullptr, nullptr));
  coordinator.RequestTypeAndVisibility(
      view_ref_info.koid, fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC, true);
}

}  // namespace
}  // namespace virtual_keyboard_coordinator
}  // namespace root_presenter
