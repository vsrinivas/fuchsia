// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/virtual_keyboard/tests/mocks/mock_virtual_keyboard_manager.h"

namespace accessibility_test {

bool MockVirtualKeyboardManager::ViewHasVisibleVirtualkeyboard(zx_koid_t view_ref_koid) {
  return view_with_virtual_keyboard_ == view_ref_koid;
}

std::optional<zx_koid_t> MockVirtualKeyboardManager::GetViewWithVisibleVirtualkeyboard() {
  return view_with_virtual_keyboard_;
}

void MockVirtualKeyboardManager::set_view_with_virtual_keyboard(
    std::optional<zx_koid_t> view_with_virtual_keyboard) {
  view_with_virtual_keyboard_ = view_with_virtual_keyboard;
}

}  //  namespace accessibility_test
