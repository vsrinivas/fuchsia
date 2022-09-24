// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIRTUAL_KEYBOARD_VIRTUAL_KEYBOARD_MANAGER_H_
#define SRC_UI_A11Y_LIB_VIRTUAL_KEYBOARD_VIRTUAL_KEYBOARD_MANAGER_H_

#include <zircon/types.h>

#include <optional>

namespace a11y {

// Interface to track which views, if any, have a visible virtual keyboard.
class VirtualKeyboardManager {
 public:
  // Returns true if the view referenced by |view_ref_koid| contains a visible virtual
  // keyboard.
  virtual bool ViewHasVisibleVirtualkeyboard(zx_koid_t view_ref_koid) = 0;

  // Returns the koid of the view that has a visible virtual keyboard if any.
  virtual std::optional<zx_koid_t> GetViewWithVisibleVirtualkeyboard() = 0;
};

}  //  namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIRTUAL_KEYBOARD_VIRTUAL_KEYBOARD_MANAGER_H_
