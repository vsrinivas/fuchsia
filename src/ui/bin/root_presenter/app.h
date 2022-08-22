// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_APP_H_
#define SRC_UI_BIN_ROOT_PRESENTER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/input/virtualkeyboard/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>

#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/ui/bin/root_presenter/constants.h"
#include "src/ui/bin/root_presenter/focus_dispatcher.h"
#include "src/ui/bin/root_presenter/inspect.h"
#include "src/ui/bin/root_presenter/presentation.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_coordinator.h"
#include "src/ui/bin/root_presenter/virtual_keyboard_manager.h"

namespace root_presenter {

// Class for serving various input and graphics related APIs.
class App {
 public:
  App(sys::ComponentContext* component_context, fit::closure quit_callback);
  ~App() = default;

  // For testing.
  Presentation* presentation() { return presentation_.get(); }

  // For testing.
  const inspect::Inspector* inspector() { return inspector_.inspector(); }

 private:
  // Exits the loop, terminating the RootPresenter process.
  void Exit() { quit_callback_(); }

  const fit::closure quit_callback_;
  sys::ComponentInspector inspector_;

  fuchsia::ui::scenic::ScenicPtr scenic_;

  // Created at construction time.
  std::unique_ptr<Presentation> presentation_;

  // Coordinates virtual keyboard state changes between
  // `fuchsia.input.virtualkeyboard.Controller`s and the
  // `fuchsia.input.virtualkeyboard.Manager`.
  FidlBoundVirtualKeyboardCoordinator virtual_keyboard_coordinator_;

  // Used to dispatch the focus change messages to interested downstream clients.
  FocusDispatcher focus_dispatcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_APP_H_
