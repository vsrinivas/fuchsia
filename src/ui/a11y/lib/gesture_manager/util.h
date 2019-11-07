// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_UTIL_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_UTIL_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>

namespace a11y {

// Converts an Accessibility pointer event to a regular pointer event.
fuchsia::ui::input::PointerEvent ToPointerEvent(
    const fuchsia::ui::input::accessibility::PointerEvent& a11y_event);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_UTIL_H_
