// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIRTUAL_KEYBOARD_TESTS_MOCKS_MOCK_VIRTUAL_KEYBOARD_MANAGER_H_
#define SRC_UI_A11Y_LIB_VIRTUAL_KEYBOARD_TESTS_MOCKS_MOCK_VIRTUAL_KEYBOARD_MANAGER_H_

#include "src/ui/a11y/lib/virtual_keyboard/virtual_keyboard_manager.h"

namespace accessibility_test {

class MockVirtualKeyboardManager : public a11y::VirtualKeyboardManager {
 public:
  // |VirtualKeyboardManager|
  //
  // Returns true if view_ref_koid == *view_with_virtual_keyboard_ and false
  // otherwise.
  bool ViewHasVisibleVirtualkeyboard(zx_koid_t view_ref_koid) override;

  // |VirtualKeyboardManager|
  //
  // Returns view_with_virtual_keyboard_.
  std::optional<zx_koid_t> GetViewWithVisibleVirtualkeyboard() override;

  // Sets the return value of `GetViewWithVisibleVirtualkeyboard`.
  void set_view_with_virtual_keyboard(std::optional<zx_koid_t> view_with_virtual_keyboard);

 private:
  // Holds the view ref koid of the view that currently has a visible virtual
  // keyboard, if any.
  std::optional<zx_koid_t> view_with_virtual_keyboard_;
};

}  //  namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_VIRTUAL_KEYBOARD_TESTS_MOCKS_MOCK_VIRTUAL_KEYBOARD_MANAGER_H_
