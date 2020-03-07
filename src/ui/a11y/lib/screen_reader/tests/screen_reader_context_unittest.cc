// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"

#include <lib/fidl/cpp/binding_set.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"

namespace accessibility_test {
class ScreenReaderContextTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    // Initialize A11yFocusManager.
    auto a11y_focus_manager =
        std::make_unique<a11y::A11yFocusManager>(&mock_focus_requester_, &mock_focus_registry_);

    // Store raw pointer to A11yFocusManager.
    a11y_focus_manager_ptr_ = a11y_focus_manager.get();

    // Initialize screen reader context.
    screen_reader_context_ =
        std::make_unique<a11y::ScreenReaderContext>(std::move(a11y_focus_manager));
  }

  MockAccessibilityFocusChainRequester mock_focus_requester_;
  MockAccessibilityFocusChainRegistry mock_focus_registry_;
  a11y::A11yFocusManager* a11y_focus_manager_ptr_ = nullptr;
  std::unique_ptr<a11y::ScreenReaderContext> screen_reader_context_;
};

// Checks that the pointer returned by GetA11yFocusManager matches the one passed in the
// constructor.
TEST_F(ScreenReaderContextTest, GetA11yFocusManager) {
  ASSERT_EQ(a11y_focus_manager_ptr_, screen_reader_context_->GetA11yFocusManager());
}

}  // namespace accessibility_test
