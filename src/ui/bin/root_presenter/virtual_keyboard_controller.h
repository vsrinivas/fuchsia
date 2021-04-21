// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_H_

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>

#include "fuchsia/ui/views/cpp/fidl.h"

namespace root_presenter {

// Allows callers to request changes in virtual keyboard configuration, and to
// watch for changes in virtual keyboard visibility.
class VirtualKeyboardController : public fuchsia::input::virtualkeyboard::Controller {
 public:
  VirtualKeyboardController(fuchsia::ui::views::ViewRef view_ref,
                            fuchsia::input::virtualkeyboard::TextType text_type);

  // |fuchsia.input.virtualkeyboard.Controller|
  // Called either via IPC, or from unit tests.
  void SetTextType(::fuchsia::input::virtualkeyboard::TextType text_type) override;
  void RequestShow() override;
  void RequestHide() override;
  void WatchVisibility(WatchVisibilityCallback callback) override;

 private:
  void MaybeNotifyWatcher();

  bool visible_;
  std::optional<bool> last_sent_visible_;
  WatchVisibilityCallback watch_callback_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_H_
