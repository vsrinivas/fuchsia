// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_COORDINATOR_H_
#define SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_COORDINATOR_H_

#include <lib/sys/cpp/component_context.h>

#include "src/ui/bin/root_presenter/virtual_keyboard_controller_creator.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

namespace root_presenter {

// Hosts a `VirtualKeyboardControllerCreator` and a `VirtualKeyboardManager`,
// and relays state changes between them.
class VirtualKeyboardCoordinator {
 public:
  explicit VirtualKeyboardCoordinator(sys::ComponentContext* component_context);

 private:
  VirtualKeyboardControllerCreator controller_creator_;
  VirtualKeyboardManager manager_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_COORDINATOR_H_
