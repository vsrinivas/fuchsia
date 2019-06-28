// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/scenic_ptr.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

namespace fuchsia {
namespace feedback {

Scenic::Scenic(std::shared_ptr<::sys::ServiceDirectory> services) : services_(services) {}

fit::promise<fuchsia::ui::scenic::ScreenshotData> Scenic::TakeScreenshot() {
  scenic_ = services_->Connect<fuchsia::ui::scenic::Scenic>();

  scenic_.set_error_handler([this](zx_status_t status) {
    if (!done_.completer) {
      return;
    }

    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.ui.scenic.Scenic";
    done_.completer.complete_error();
  });

  scenic_->TakeScreenshot([this](fuchsia::ui::scenic::ScreenshotData raw_screenshot, bool success) {
    if (!done_.completer) {
      return;
    }

    if (!success) {
      FX_LOGS(ERROR) << "Scenic failed to take screenshot";
      done_.completer.complete_error();
    } else {
      done_.completer.complete_ok(std::move(raw_screenshot));
    }
  });

  return done_.consumer.promise_or(fit::error());
}

}  // namespace feedback
}  // namespace fuchsia
