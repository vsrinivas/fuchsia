// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_TOUCH_DISPATCHER_APP_H_
#define GARNET_BIN_A11Y_A11Y_TOUCH_DISPATCHER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/bin/a11y/a11y_touch_dispatcher/a11y_touch_dispatcher_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace a11y_touch_dispatcher {

// A11y touch dispatcher entry point
class App {
 public:
  App();
  ~App() = default;

 private:
  std::unique_ptr<component::StartupContext> startup_context_;
  std::unique_ptr<A11yTouchDispatcherImpl> touch_dispatcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace a11y_touch_dispatcher

#endif  // GARNET_BIN_A11Y_A11Y_TOUCH_DISPATCHER_APP_H_
