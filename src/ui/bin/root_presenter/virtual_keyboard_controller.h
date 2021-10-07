// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_H_

#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>

#include "fuchsia/ui/views/cpp/fidl.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"

namespace root_presenter {

class VirtualKeyboardCoordinator;

// Allows callers to request changes in virtual keyboard configuration, and to
// watch for changes in virtual keyboard visibility.
class VirtualKeyboardController : public fuchsia::input::virtualkeyboard::Controller {
 public:
  enum class UserAction { HIDE_KEYBOARD, SHOW_KEYBOARD };

  virtual ~VirtualKeyboardController() = default;

  // Called by VirtualKeyboardCoordinator.
  virtual void OnUserAction(UserAction action) = 0;
};

class FidlBoundVirtualKeyboardController : public VirtualKeyboardController {
 public:
  FidlBoundVirtualKeyboardController(fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator,
                                     zx_koid_t view_koid,
                                     fuchsia::input::virtualkeyboard::TextType text_type);
  ~FidlBoundVirtualKeyboardController() override;

  // |fuchsia.input.virtualkeyboard.Controller|
  // Called either via IPC, or from unit tests.
  void SetTextType(::fuchsia::input::virtualkeyboard::TextType text_type) override;
  void RequestShow() override;
  void RequestHide() override;
  void WatchVisibility(WatchVisibilityCallback callback) override;

  // Informs `this` that the ground-truth of keyboard visibility has changed, due
  // to the user's interaction with the keyboard.
  //
  // This enables the Controller to avoid inadvertently overriding the user's
  // intent. For example, after the user presses the dismiss button on the
  // keyboard, SetTextType() will not have the side-effect of re-opening
  // the keyboard.
  //
  // Called by VirtualKeyboardCoordinator.
  void OnUserAction(UserAction action) override;

 private:
  void MaybeNotifyWatcher();
  void NotifyCoordinator();

  fxl::WeakPtr<VirtualKeyboardCoordinator> coordinator_;

  zx_koid_t view_koid_{};

  // The type of text for which `this` wants to enable input.
  //
  // `text_type_` is cached so that `this` can send that information
  // to `coordinator_` when RequestShow() or RequestHide() is called.
  //
  // The value may differ from the ground truth about which text type
  // is supported by a visible keyboard, as that truth is owned by the
  // client of the `fuchsia.input.virtualkeyboard.Manager` protocol.
  //
  // Currently, `text_type_` differs from the actual visibility only during the
  // transient periods when `text_type_` has been updated, and the Manager
  // client has not read a new value from
  // fuchsia.input.virtualkeyboard.Manager.WatchTypeAndVisibility().
  //
  // In the future, `text_type_` may also differ from the ground truth when
  // `this` is unable to effect its desire, because the corresponding
  // `fuchsia.ui.views.View` does not have focus.
  fuchsia::input::virtualkeyboard::TextType text_type_;

  // Whether or not `this` wants the keyboard to be visible.
  //
  // `want_visible_` is cached so that `this` can send the visibility bit
  // to `coordinator_` when SetTextType() is called.
  //
  // The value may differ from the ground truth about visibility, which is owned by
  // the client of the `fuchsia.input.virtualkeyboard.Manager` protocol.
  //
  // Currently, `want_visible_` differs from the actual visibility only during the
  // transient periods when either
  // a) `want_visible_` has been updated, and the Manager client has not read a new
  //    value from fuchsia.input.virtualkeyboard.Manager.WatchTypeAndVisibility()
  // b) the user has dismissed the keyboard using the virtual keyboard GUI, and
  //    `want_visible_` has not yet observed the change
  //
  // In the future, `want_visible_` may also differ from the ground truth when
  // `this` is unable to effect its desire, because the corresponding
  // `fuchsia.ui.views.View` does not have focus.
  bool want_visible_;

  // The visibility last sent on the `fuchsia.input.virtualkeyboard.Controller`
  // channel bound to `this`.
  // * used to
  //   * identify the first call to WatchVisibility()
  //   * avoid sending no-op responses on later calls to WatchVisibility()
  // * equal to `nullopt`, iff the client has never called WatchTypeAndVisibility()
  std::optional<bool> last_sent_visible_;

  WatchVisibilityCallback watch_callback_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_VIRTUAL_KEYBOARD_CONTROLLER_H_
