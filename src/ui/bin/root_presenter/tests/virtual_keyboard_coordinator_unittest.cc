// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace root_presenter {
namespace virtual_keyboard_coordinator {
namespace {

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

TEST_F(VirtualKeyboardCoordinatorTest, NotifyVisibilityChangeDoesNotCrash) {
  FidlBoundVirtualKeyboardCoordinator coordinator(context_provider()->context());
  coordinator.NotifyVisibilityChange(
      false, fuchsia::input::virtualkeyboard::VisibilityChangeReason::USER_INTERACTION);
}

TEST_F(VirtualKeyboardCoordinatorTest, RequestTypeAndVisibilityDoesNotCrash) {
  FidlBoundVirtualKeyboardCoordinator coordinator(context_provider()->context());
  coordinator.RequestTypeAndVisibility(fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC,
                                       true);
}

}  // namespace
}  // namespace virtual_keyboard_coordinator
}  // namespace root_presenter
