// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_KEY_UTIL_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_KEY_UTIL_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>
#include <hid/usages.h>

#include <tuple>

namespace root_presenter {

// Utility function to create Key event from Keyboard event.
std::optional<fuchsia::ui::input2::KeyEvent> into_key_event(
    const fuchsia::ui::input::KeyboardEvent& event);

// Utility function to convert USB HID code to a Fuchsia Key.
std::optional<fuchsia::ui::input2::Key> into_key(uint32_t hid);

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_KEY_UTIL_H_
