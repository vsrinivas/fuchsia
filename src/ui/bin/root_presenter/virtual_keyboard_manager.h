// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_MANAGER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_MANAGER_H_

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>

#include "fuchsia/ui/views/cpp/fidl.h"
#include "lib/sys/cpp/component_context.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace root_presenter {

class VirtualKeyboardCoordinator;

// Allows the virtual keyboard GUI to synchronize virtual keyboard state with the platform.
class VirtualKeyboardManager : public fuchsia::input::virtualkeyboard::Manager {
 public:
  explicit VirtualKeyboardManager(fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator,
                                  sys::ComponentContext* component_context,
                                  fuchsia::input::virtualkeyboard::TextType initial_text_type);

  // |fuchsia.input.virtualkeyboard.Manager|
  // Called either via IPC, or from unit tests.
  void WatchTypeAndVisibility(WatchTypeAndVisibilityCallback callback) override;
  void Notify(bool is_visible, fuchsia::input::virtualkeyboard::VisibilityChangeReason reason,
              NotifyCallback callback) override;

  // Updates the desired TextType and visibility of the virtual keyboard, and
  // responds to the hanging get to WatchTypeAndVisibility(), if one exists.
  //
  // Called by VirtualKeyboardCoordinator.
  void OnTypeOrVisibilityChange(fuchsia::input::virtualkeyboard::TextType text_type,
                                bool is_visible);

 private:
  struct KeyboardConfig {
    fuchsia::input::virtualkeyboard::TextType text_type;
    bool is_visible;
    bool operator!=(const KeyboardConfig& other) const {
      return text_type != other.text_type || is_visible != other.is_visible;
    }
  };

  // Binds `request` to `manager_binding_`, iff `manager_binding_` is unbound.
  void MaybeBind(fidl::InterfaceRequest<fuchsia::input::virtualkeyboard::Manager> request);

  // Responds to the hanging get to WatchTypeAndVisibility(), iff
  // * there is a hanging get pending, and
  // * pending_config_ difers from last_sent_config_
  void MaybeNotifyWatcher();

  // The configuration last sent on the FIDL channel owned by `this`.
  // * used to
  //   * identify the first call to WatchTypeAndVisibility()
  //   * avoid sending no-op responses on later calls to WatchTypeAndVisibility()
  // * equal to `nullopt`, iff the client has never called WatchTypeAndVisibility()
  std::optional<KeyboardConfig> last_sent_config_;

  // The configuration to send on the next call to WatchTypeAndVisibility().
  //
  // * Used to buffer configuration changes when there is no pending
  //   WatchTypeAndVisibility() call.
  // * Equal to `nullopt`, except in the transient state where `this` has
  //   responded to WatchTypeAndVisibility(), received a configuration change,
  //   but not received another call to WatchTypeAndVsibility().
  std::optional<KeyboardConfig> pending_config_;

  fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator_;
  fidl::Binding<fuchsia::input::virtualkeyboard::Manager> manager_binding_;
  WatchTypeAndVisibilityCallback watch_callback_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_MANAGER_H_
