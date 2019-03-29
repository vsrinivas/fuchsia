// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <utility>

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/logging.h"

namespace {

class A11yToggler {
 public:
  A11yToggler(fit::closure quit_callback);
  void ToggleAccessibilitySupport(bool enabled);

 private:
  fit::closure quit_callback_;
  fuchsia::accessibility::TogglerPtr a11y_toggler_;
};

A11yToggler::A11yToggler(fit::closure quit_callback)
    : quit_callback_(std::move(quit_callback)) {
  FXL_DCHECK(quit_callback_);
  auto context_ = sys::ComponentContext::Create();
  context_->svc()->Connect(a11y_toggler_.NewRequest());
  a11y_toggler_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << "Connection error connecting to a11y toggler.";
    quit_callback_();
  });
}

void A11yToggler::ToggleAccessibilitySupport(bool enabled) {
  a11y_toggler_->ToggleAccessibilitySupport(enabled);
}

}  // namespace

int main(int argc, const char** argv) {
  if (argc < 2) {
    FXL_LOG(INFO) << "usage: a11y_toggler [true/false]";
    return -1;
  }

  bool enabled = false;

  if (strcmp("false", argv[1]) == 0) {
    enabled = false;
    FXL_LOG(INFO) << "Disabling accessibility support";
  } else if (strcmp("true", argv[1]) == 0) {
    enabled = true;
    FXL_LOG(INFO) << "Enabling accessibility support";
  } else {
    FXL_LOG(INFO) << "usage: a11y_toggler [true/false]";
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  A11yToggler toggler([&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });

  async::PostTask(loop.dispatcher(), [&]() {
    toggler.ToggleAccessibilitySupport(enabled);
    loop.Quit();
  });
  loop.Run();

  return 0;
}
