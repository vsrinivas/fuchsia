// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TALKBACK_APP_H_
#define GARNET_BIN_A11Y_TALKBACK_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/bin/a11y/talkback/gesture_detector.h"
#include "garnet/bin/a11y/talkback/talkback_impl.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace talkback {

// Talkback application entry point.
class App {
 public:
  explicit App();
  ~App() = default;

 private:
  std::unique_ptr<component::StartupContext> startup_context_;

  std::unique_ptr<TalkbackImpl> talkback_;
  std::unique_ptr<GestureDetector> gesture_detector_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace talkback

#endif  // GARNET_BIN_A11Y_TALKBACK_APP_H_
