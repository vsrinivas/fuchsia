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

TEST_F(VirtualKeyboardControllerTest, CtorDoesNotCrash) {
  VirtualKeyboardController(view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC);
}

TEST_F(VirtualKeyboardControllerTest, SetTextTypeDoesNotCrash) {
  VirtualKeyboardController(view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .SetTextType(fuchsia::input::virtualkeyboard::TextType::NUMERIC);
}

TEST_F(VirtualKeyboardControllerTest, RequestShowDoesNotCrash) {
  VirtualKeyboardController(view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .RequestShow();
}

TEST_F(VirtualKeyboardControllerTest, RequestHideDoesNotCrash) {
  VirtualKeyboardController(view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .RequestHide();
}

TEST_F(VirtualKeyboardControllerTest, WatchVisibilityDoesNotCrash) {
  VirtualKeyboardController(view_ref(), fuchsia::input::virtualkeyboard::TextType::ALPHANUMERIC)
      .WatchVisibility([](bool updated_visibility) {});
}

}  // namespace
}  // namespace virtual_keyboard_controller
}  // namespace root_presenter
