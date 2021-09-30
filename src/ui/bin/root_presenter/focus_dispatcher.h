// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_FOCUS_DISPATCHER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_FOCUS_DISPATCHER_H_

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/keyboard/focus/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/bin/root_presenter/focus_listener.h"

namespace root_presenter {

// Forwards the focus change messages from fuchsia.ui.focus.FocusChainListener to
// fuchsia.ui.keyboard.focus.Controller.
//
// When constructed via `FocusDispatcher::New()`, it registers itself as a handler
// for `OnFocusChange` notifications from `fuchsia.ui.focus.FocusChainListener`.
//
// When a focus change notification comes in, the information about the most precise
// view reference is forwarded on to `fuchsia.ui.keyboard.focus.Controller.Notify`,
// and to `local_focus_listener`.
class FocusDispatcher : public fuchsia::ui::focus::FocusChainListener {
 public:
  // Makes a new `FocusDispatcher`.
  //
  // * `svc` is a directory into which to serve Controller.
  // * `local_focus_listener` is the local object which should receive focus updates
  FocusDispatcher(const std::shared_ptr<sys::ServiceDirectory>& svc,
                  fxl::WeakPtr<FocusListener> local_focus_listener);

  // Implements `fuchsia.ui.focus.FocusChainListener`.
  //
  // When an `OnFocusChange` message arrives, it is sent to `keyboard.focus.Controller.Notify`.
  void OnFocusChange(
      fuchsia::ui::focus::FocusChain new_focus_chain,
      fuchsia::ui::focus::FocusChainListener::OnFocusChangeCallback callback) override;

 private:
  // A client-side connection to Controller.
  fidl::InterfacePtr<fuchsia::ui::keyboard::focus::Controller> keyboard_focus_ctl_;

  // A server-side binding to FocusChainListener.
  fidl::BindingSet<fuchsia::ui::focus::FocusChainListener> focus_chain_listeners_;

  // Reference to a local object which is also interested in focus changes.
  fxl::WeakPtr<FocusListener> local_focus_listener_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_FOCUS_DISPATCHER_H_
