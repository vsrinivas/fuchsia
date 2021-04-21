// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_MANAGER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_MANAGER_H_

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>

#include "fuchsia/ui/views/cpp/fidl.h"
#include "lib/sys/cpp/component_context.h"

namespace root_presenter {

// Allows the virtual keyboard GUI to synchronize virtual keyboard state with the platform.
class VirtualKeyboardManager : public fuchsia::input::virtualkeyboard::Manager {
 public:
  explicit VirtualKeyboardManager(sys::ComponentContext* component_context);

  // |fuchsia.input.virtualkeyboard.Manager|
  // Called either via IPC, or from unit tests.
  void WatchTypeAndVisibility(WatchTypeAndVisibilityCallback callback) override;
  void Notify(bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason,
              NotifyCallback callback) override;

 private:
  void MaybeBind(fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request);

  fidl::Binding<fuchsia::input::virtualkeyboard::Manager> manager_binding_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_MANAGER_H_
