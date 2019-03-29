// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_FUCHSIAVOX_APP_H_
#define GARNET_BIN_A11Y_FUCHSIAVOX_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "garnet/bin/a11y/fuchsiavox/fuchsiavox_impl.h"
#include "garnet/bin/a11y/fuchsiavox/gesture_detector.h"
#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace fuchsiavox {

// Fuchsiavox application entry point.
class App {
 public:
  explicit App();
  ~App() = default;

 private:
  std::unique_ptr<sys::ComponentContext> startup_context_;

  std::unique_ptr<FuchsiavoxImpl> fuchsiavox_;
  std::unique_ptr<GestureDetector> gesture_detector_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace fuchsiavox

#endif  // GARNET_BIN_A11Y_FUCHSIAVOX_APP_H_
